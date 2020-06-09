/*
 * Copyright 2016-2018 Alex Beregszaszi et al.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hera/hera.h>

#include <limits>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <map>
#include <memory>

#include <evmc/evmc.h>

#include "debugging.h"
#include "eei.h"
#include "exceptions.h"
#include "helpers.h"
#if HERA_BINARYEN
#include "binaryen.h"
#endif
#if HERA_WAVM
#include "wavm.h"
#endif
#if HERA_WABT
#include "wabt.h"
#endif

#include <hera/buildinfo.h>

using namespace std;
using namespace hera;

namespace {

enum class hera_evm1mode {
  reject,
  fallback,
  evm2wasm_contract,
  runevm_contract,
};

const map<string, hera_evm1mode> evm1mode_options {
  { "reject", hera_evm1mode::reject },
  { "fallback", hera_evm1mode::fallback },
  { "evm2wasm", hera_evm1mode::evm2wasm_contract },
  { "runevm", hera_evm1mode::runevm_contract },
};

using WasmEngineCreateFn = unique_ptr<WasmEngine>(*)();

const map<string, WasmEngineCreateFn> wasm_engine_map {
#if HERA_BINARYEN
  { "binaryen", BinaryenEngine::create },
#endif
#if HERA_WAVM
  { "wavm", WavmEngine::create },
#endif
#if HERA_WABT
  { "wabt", WabtEngine::create },
#endif
};

WasmEngineCreateFn wasmEngineCreateFn =
// This is the order of preference.
#if HERA_BINARYEN
    BinaryenEngine::create
#elif HERA_WABT
    WabtEngine::create
#elif HERA_WAVM
    WavmEngine::create
#else
#error "No engine requested."
#endif
;

struct hera_instance : evmc_vm {
  unique_ptr<WasmEngine> engine = wasmEngineCreateFn();
  hera_evm1mode evm1mode = hera_evm1mode::reject;
  bool metering = false;
  map<evmc::address, bytes> contract_preload_list;

  hera_instance() noexcept : evmc_vm({EVMC_ABI_VERSION, "hera", hera_get_buildinfo()->project_version, nullptr, nullptr, nullptr, nullptr}) {}
};

using namespace evmc::literals;

constexpr auto sentinelAddress = 0x000000000000000000000000000000000000000a_address;
constexpr auto evm2wasmAddress = 0x000000000000000000000000000000000000000b_address;
constexpr auto runevmAddress = 0x000000000000000000000000000000000000000c_address;

// Calls a system contract at @address with input data @input.
// It is a "staticcall" with sender 000...000 and no value.
// @returns output data from the contract and update the @gas variable with the gas left.
pair<evmc_status_code, bytes> callSystemContract(
  evmc::HostContext& context,
  evmc_address const& address,
  int64_t & gas,
  bytes_view input
) {
  evmc_message message = {
    .kind = EVMC_CALL,
    .flags = EVMC_STATIC,
    .depth = 0,
    .gas = gas,
    .destination = address,
    .sender = {},
    .input_data = input.data(),
    .input_size = input.size(),
    .value = {},
    .create2_salt = {},
  };

  evmc::result result = context.call(message);

  bytes ret;
  if (result.status_code == EVMC_SUCCESS && result.output_data)
    ret.assign(result.output_data, result.output_data + result.output_size);

  gas = result.gas_left;

  return {result.status_code, ret};
}

pair<evmc_status_code, bytes> locallyExecuteSystemContract(
  evmc::HostContext& context,
  evmc_address const& address,
  int64_t & gas,
  bytes_view input,
  bytes_view code,
  bytes_view state_code
) {
  const evmc_message message = {
    .kind = EVMC_CALL,
    .flags = EVMC_STATIC,
    .depth = 0,
    .gas = gas,
    .destination = address,
    .sender = {},
    .input_data = input.data(),
    .input_size = input.size(),
    .value = {},
    .create2_salt = {},
  };

  unique_ptr<WasmEngine> engine = wasmEngineCreateFn();
  // TODO: should we catch exceptions here?
  ExecutionResult result = engine->execute(context, code, state_code, message, false);

  bytes ret;
  evmc_status_code status = result.isRevert ? EVMC_REVERT : EVMC_SUCCESS;
  if (status == EVMC_SUCCESS && result.returnValue.size() > 0)
    ret = move(result.returnValue);

  return {status, move(ret)};
}

// Calls the Sentinel contract with input data @input.
// @returns the validated and metered output or empty output otherwise.
bytes sentinel(evmc::HostContext& context, bytes_view input)
{
  HERA_DEBUG << "Metering (input " << input.size() << " bytes)...\n";

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  evmc_status_code status;
  bytes ret;

  tie(status, ret) = callSystemContract(
    context,
    sentinelAddress,
    gas,
    input
  );

  HERA_DEBUG << "Metering done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas) with code=" << status << "\n";

  ensureCondition(
    status == EVMC_SUCCESS,
    ContractValidationFailure,
    "Sentinel has failed on contract. It is invalid."
  );

  return ret;
}

// Calls the evm2wasm contract with input data @input.
// @returns the compiled output or empty output otherwise.
bytes evm2wasm(evmc::HostContext& context, bytes_view input) {
  HERA_DEBUG << "Calling evm2wasm (input " << input.size() << " bytes)...\n";

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  evmc_status_code status;
  bytes ret;

  tie(status, ret) = callSystemContract(
    context,
    evm2wasmAddress,
    gas,
    input
  );

  HERA_DEBUG << "evm2wasm done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas) with status=" << status << "\n";

  ensureCondition(
    status == EVMC_SUCCESS,
    ContractValidationFailure,
    "evm2wasm has failed."
  );

  return ret;
}

// Calls the runevm contract.
// @returns a wasm-based evm interpreter.
bytes runevm(evmc::HostContext& context, bytes code) {
  HERA_DEBUG << "Calling runevm (code " << code.size() << " bytes)...\n";

  int64_t gas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  evmc_status_code status;
  bytes ret;

  tie(status, ret) = locallyExecuteSystemContract(
      context,
      runevmAddress,
      gas,
      {},
      code,
      code
  );

  HERA_DEBUG << "runevm done (output " << ret.size() << " bytes) with status=" << status << "\n";

  ensureCondition(
    status == EVMC_SUCCESS,
    ContractValidationFailure,
    "runevm has failed."
  );
  ensureCondition(
    ret.size() > 0,
    ContractValidationFailure,
    "Runevm returned empty."
  );
  ensureCondition(
    hasWasmPreamble(ret),
    ContractValidationFailure,
    "Runevm result has no wasm preamble."
  );

  return ret;
}

void hera_destroy_result(evmc_result const* result) noexcept
{
  delete[] result->output_data;
}

evmc_result hera_execute(
  evmc_vm *vm,
  const evmc_host_interface* host_interface,
  evmc_host_context *context,
  enum evmc_revision rev,
  const evmc_message *msg,
  const uint8_t *code,
  size_t code_size
) noexcept {
  hera_instance* hera = static_cast<hera_instance*>(vm);
  evmc::HostContext host{*host_interface, context};

  HERA_DEBUG << "Executing message in Hera\n";

  evmc_result ret;
  memset(&ret, 0, sizeof(evmc_result));

  try {
    heraAssert(rev == EVMC_BYZANTIUM, "Only Byzantium supported.");
    heraAssert(msg->gas >= 0, "EVMC supplied negative startgas");

    bool meterInterfaceGas = true;

    // the bytecode residing in the state - this will be used by interface methods (i.e. codecopy)
    bytes_view state_code{code, code_size};

    // the actual executable code - this can be modified (metered or evm2wasm compiled)
    bytes run_code{state_code};

    // replace executable code if replacement is supplied
    auto preload = hera->contract_preload_list.find(msg->destination);
    if (preload != hera->contract_preload_list.end()) {
      HERA_DEBUG << "Overriding contract.\n";
      run_code = preload->second;
    }

    // ensure we can only handle WebAssembly version 1
    bool isWasm = hasWasmPreamble(run_code);

    if (!isWasm) {
      switch (hera->evm1mode) {
      case hera_evm1mode::evm2wasm_contract:
        run_code = evm2wasm(host, run_code);
        ensureCondition(run_code.size() > 8, ContractValidationFailure, "Transcompiling via evm2wasm failed");
        // TODO: enable this once evm2wasm does metering of interfaces
        // meterInterfaceGas = false;
        break;
      case hera_evm1mode::fallback:
        HERA_DEBUG << "Non-WebAssembly input, but fallback mode enabled, asking client to deal with it.\n";
        ret.status_code = EVMC_REJECTED;
        return ret;
      case hera_evm1mode::reject:
        HERA_DEBUG << "Non-WebAssembly input, failure.\n";
        ret.status_code = EVMC_FAILURE;
        return ret;
      case hera_evm1mode::runevm_contract:
        run_code = runevm(host, hera->contract_preload_list[runevmAddress]);
        ensureCondition(run_code.size() > 8, ContractValidationFailure, "Interpreting via runevm failed");
        // Runevm does interface metering on its own
        meterInterfaceGas = false;
        break;
      }
    }

    ensureCondition(
      hasWasmVersion(run_code, 1),
      ContractValidationFailure,
      "Contract has an invalid WebAssembly version."
    );

    // Avoid this in case of evm2wasm translated code
    if (msg->kind == EVMC_CREATE && isWasm) {
      // Meter the deployment (constructor) code if it is WebAssembly
      if (hera->metering)
        run_code = sentinel(host, run_code);
      ensureCondition(
        hasWasmPreamble(run_code) && hasWasmVersion(run_code, 1),
        ContractValidationFailure,
        "Invalid contract or metering failed."
      );
    }

    heraAssert(hera->engine, "Wasm engine not set.");
    WasmEngine& engine = *hera->engine;

    ExecutionResult result = engine.execute(host, run_code, state_code, *msg, meterInterfaceGas);
    heraAssert(result.gasLeft >= 0, "Negative gas left after execution.");

    // copy call result
    if (result.returnValue.size() > 0) {
      bytes returnValue;

      if (msg->kind == EVMC_CREATE && !result.isRevert && hasWasmPreamble(result.returnValue)) {
        ensureCondition(
          hasWasmVersion(result.returnValue, 1),
          ContractValidationFailure,
          "Contract has an invalid WebAssembly version."
        );

        // Meter the deployed code if it is WebAssembly
        returnValue = hera->metering ? sentinel(host, result.returnValue) : move(result.returnValue);
        ensureCondition(
          hasWasmPreamble(returnValue) && hasWasmVersion(returnValue, 1),
          ContractValidationFailure,
          "Invalid contract or metering failed."
        );
        // FIXME: this should be done by the sentinel
        engine.verifyContract({returnValue.data(), returnValue.size()});
      } else {
        returnValue = move(result.returnValue);
      }

      uint8_t* output_data = new uint8_t[returnValue.size()];
      copy(returnValue.begin(), returnValue.end(), output_data);

      ret.output_size = returnValue.size();
      ret.output_data = output_data;
      ret.release = hera_destroy_result;
    }

    ret.status_code = result.isRevert ? EVMC_REVERT : EVMC_SUCCESS;
    ret.gas_left = result.gasLeft;
  } catch (EndExecution const&) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    HERA_DEBUG << "EndExecution exception has leaked through.\n";
  } catch (VMTrap const& e) {
    // TODO: use specific error code? EVMC_INVALID_INSTRUCTION or EVMC_TRAP_INSTRUCTION?
    ret.status_code = EVMC_FAILURE;
    HERA_DEBUG << e.what() << "\n";
  } catch (ArgumentOutOfRange const& e) {
    ret.status_code = EVMC_ARGUMENT_OUT_OF_RANGE;
    HERA_DEBUG << e.what() << "\n";
  } catch (OutOfGas const& e) {
    ret.status_code = EVMC_OUT_OF_GAS;
    HERA_DEBUG << e.what() << "\n";
  } catch (ContractValidationFailure const& e) {
    ret.status_code = EVMC_CONTRACT_VALIDATION_FAILURE;
    HERA_DEBUG << e.what() << "\n";
  } catch (InvalidMemoryAccess const& e) {
    ret.status_code = EVMC_INVALID_MEMORY_ACCESS;
    HERA_DEBUG << e.what() << "\n";
  } catch (StaticModeViolation const& e) {
    ret.status_code = EVMC_STATIC_MODE_VIOLATION;
    HERA_DEBUG << e.what() << "\n";
  } catch (InternalErrorException const& e) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    HERA_DEBUG << "InternalError: " << e.what() << "\n";
  } catch (exception const& e) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    HERA_DEBUG << "Unknown exception: " << e.what() << "\n";
  } catch (...) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    HERA_DEBUG << "Totally unknown exception\n";
  }

  return ret;
}

bool hera_parse_sys_option(hera_instance *hera, string const& _name, string const& value)
{
  heraAssert(_name.find("sys:") == 0, "");
  string name = _name.substr(4, string::npos);
  evmc_address address{};

  if (name.find("0x") == 0) {
    // hex address
    bytes ret = parseHexString(name.substr(2, string::npos));
    if (ret.empty()) {
      HERA_DEBUG << "Failed to parse hex address: " << name << "\n";
      return false;
    }
    if (ret.size() != 20) {
      HERA_DEBUG << "Invalid address: " << name << "\n";
      return false;
    }

    copy(ret.begin(), ret.end(), address.bytes);
  } else {
    // alias
    const map<string, evmc_address> aliases = {
      { string("sentinel"), sentinelAddress },
      { string("evm2wasm"), evm2wasmAddress },
      { string("runevm"), runevmAddress },
    };

    if (aliases.count(name) == 0) {
      HERA_DEBUG << "Failed to resolve system contract alias: " << name << "\n";
      return false;
    }

    address = aliases.at(name);
  }

  bytes contents = loadFileContents(value);
  if (contents.size() == 0) {
    HERA_DEBUG << "Failed to load contract source (or empty): " << value << "\n";
    return false;
  }

  HERA_DEBUG << "Loaded contract for " << name << " from " << value << " (" << contents.size() << " bytes)\n";

  hera->contract_preload_list[address] = move(contents);

  return true;
}

evmc_set_option_result hera_set_option(
  evmc_vm* vm,
  char const *name,
  char const *value
) noexcept {
  hera_instance* hera = static_cast<hera_instance*>(vm);

  if (strcmp(name, "evm1mode") == 0) {
    if (evm1mode_options.count(value)) {
      hera->evm1mode = evm1mode_options.at(value);
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strcmp(name, "metering") == 0) {
    if (strcmp(value, "true") == 0)
      hera->metering = true;
    else if (strcmp(value, "false") != 0)
      return EVMC_SET_OPTION_INVALID_VALUE;
    return EVMC_SET_OPTION_SUCCESS;
  }

  if (strcmp(name, "benchmark") == 0) {
    if (strcmp(value, "true") == 0) {
      WasmEngine::enableBenchmarking();
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strcmp(name, "engine") == 0) {
    auto it = wasm_engine_map.find(value);
    if (it != wasm_engine_map.end()) {
      wasmEngineCreateFn = it->second;
      hera->engine = wasmEngineCreateFn();
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strncmp(name, "sys:", 4) == 0) {
    if (hera_parse_sys_option(hera, string(name), string(value)))
      return EVMC_SET_OPTION_SUCCESS;
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  return EVMC_SET_OPTION_INVALID_NAME;
}

void hera_destroy(evmc_vm* vm) noexcept
{
  hera_instance* hera = static_cast<hera_instance*>(vm);
  delete hera;
}

evmc_capabilities_flagset hera_get_capabilities(evmc_vm* vm)
{
  evmc_capabilities_flagset caps = EVMC_CAPABILITY_EWASM;
  if (static_cast<hera_instance*>(vm)->evm1mode != hera_evm1mode::reject)
    caps |= EVMC_CAPABILITY_EVM1;
  return caps;
}

} // anonymous namespace

extern "C" {

evmc_vm* evmc_create_hera() noexcept
{
  hera_instance* instance = new hera_instance;
  instance->destroy = hera_destroy;
  instance->execute = hera_execute;
  instance->get_capabilities = hera_get_capabilities;
  instance->set_option = hera_set_option;
  return instance;
}

#if hera_EXPORTS
// If compiled as shared library, also export this symbol.
EVMC_EXPORT evmc_vm* evmc_create() noexcept
{
  return evmc_create_hera();
}
#endif

}
