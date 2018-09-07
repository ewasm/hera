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
#include <vector>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>

#include <evmc/evmc.h>

#include "binaryen.h"
#include "debugging.h"
#include "eei.h"
#include "exceptions.h"
#include "helpers.h"
#if HERA_WAVM
#include "wavm.h"
#endif
#if HERA_WABT
#include "wabt.h"
#endif

#include <hera/buildinfo.h>

using namespace std;
using namespace hera;

// FIXME: should be part of EVMC
bool operator==(evmc_address const& lhs, evmc_address const& rhs) {
  return memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

bool operator<(evmc_address const& lhs, evmc_address const& rhs) {
  return memcmp(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) < 0;
}

namespace {

enum class hera_evm1mode {
  reject,
  fallback,
  evm2wasm_contract,
};

using WasmEngineCreateFn = unique_ptr<WasmEngine>(*)();

const map<string, WasmEngineCreateFn> wasm_engine_map {
  { "binaryen", BinaryenEngine::create },
#if HERA_WAVM
  { "wavm", WavmEngine::create },
#endif
#if HERA_WABT
  { "wabt", WabtEngine::create },
#endif
};

const map<string, hera_evm1mode> evm1mode_options {
  { "reject", hera_evm1mode::reject },
  { "fallback", hera_evm1mode::fallback },
  { "evm2wasm", hera_evm1mode::evm2wasm_contract },
};

struct hera_instance : evmc_instance {
  unique_ptr<WasmEngine> engine{new BinaryenEngine};
  hera_evm1mode evm1mode = hera_evm1mode::reject;
  bool metering = false;
  map<evmc_address, vector<uint8_t>> contract_preload_list;

  hera_instance() noexcept : evmc_instance({EVMC_ABI_VERSION, "hera", hera_get_buildinfo()->project_version, nullptr, nullptr, nullptr, nullptr, nullptr}) {}
};

const evmc_address sentinelAddress = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa } };
const evmc_address evm2wasmAddress = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb } };

// Calls a system contract at @address with input data @input.
// It is a "staticcall" with sender 000...000 and no value.
// @returns output data from the contract and update the @gas variable with the gas left.
pair<evmc_status_code, vector<uint8_t>> callSystemContract(
  evmc_context* context,
  evmc_address const& address,
  int64_t & gas,
  vector<uint8_t> const& input
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

  evmc_result result = context->host->call(context, &message);

  vector<uint8_t> ret;
  if (result.status_code == EVMC_SUCCESS && result.output_data)
    ret.assign(result.output_data, result.output_data + result.output_size);

  gas = result.gas_left;

  if (result.release)
    result.release(&result);

  return make_pair(result.status_code, ret);
}

// Calls the Sentinel contract with input data @input.
// @returns the validated and metered output or empty output otherwise.
vector<uint8_t> sentinel(evmc_context* context, vector<uint8_t> const& input)
{
  HERA_DEBUG << "Metering (input " << input.size() << " bytes)...\n";

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  evmc_status_code status;
  vector<uint8_t> ret;

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
vector<uint8_t> evm2wasm(evmc_context* context, vector<uint8_t> const& input) {
  HERA_DEBUG << "Calling evm2wasm (input " << input.size() << " bytes)...\n";

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  evmc_status_code status;
  vector<uint8_t> ret;

  tie(status, ret) = callSystemContract(
    context,
    evm2wasmAddress,
    gas,
    input
  );

  HERA_DEBUG << "evm2wasm done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas) with status=" << status << "\n";

  return ret;
}

void hera_destroy_result(evmc_result const* result) noexcept
{
  delete[] result->output_data;
}

evmc_result hera_execute(
  evmc_instance *instance,
  evmc_context *context,
  enum evmc_revision rev,
  const evmc_message *msg,
  const uint8_t *code,
  size_t code_size
) noexcept {
  hera_instance* hera = static_cast<hera_instance*>(instance);

  HERA_DEBUG << "Executing message in Hera\n";

  evmc_result ret;
  memset(&ret, 0, sizeof(evmc_result));

  try {
    heraAssert(rev == EVMC_BYZANTIUM, "Only Byzantium supported.");
    heraAssert(msg->gas >= 0, "EVMC supplied negative startgas");

    bool meterInterfaceGas = true;

    // the bytecode residing in the state - this will be used by interface methods (i.e. codecopy)
    vector<uint8_t> state_code(code, code + code_size);

    // the actual executable code - this can be modified (metered or evm2wasm compiled)
    vector<uint8_t> run_code(code, code + code_size);

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
        run_code = evm2wasm(context, run_code);
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
        run_code = sentinel(context, run_code);
      ensureCondition(
        hasWasmPreamble(run_code) && hasWasmVersion(run_code, 1),
        ContractValidationFailure,
        "Invalid contract or metering failed."
      );
    }

    heraAssert(hera->engine, "Wasm engine not set.");
    WasmEngine& engine = *hera->engine;

    ExecutionResult result = engine.execute(context, run_code, state_code, *msg, meterInterfaceGas);
    heraAssert(result.gasLeft >= 0, "Negative gas left after execution.");

    // copy call result
    if (result.returnValue.size() > 0) {
      vector<uint8_t> returnValue;

      if (msg->kind == EVMC_CREATE && !result.isRevert && hasWasmPreamble(result.returnValue)) {
        ensureCondition(
          hasWasmVersion(result.returnValue, 1),
          ContractValidationFailure,
          "Contract has an invalid WebAssembly version."
        );

        // Meter the deployed code if it is WebAssembly
        returnValue = hera->metering ? sentinel(context, result.returnValue) : move(result.returnValue);
        ensureCondition(
          hasWasmPreamble(returnValue) && hasWasmVersion(returnValue, 1),
          ContractValidationFailure,
          "Invalid contract or metering failed."
        );
        // FIXME: this should be done by the sentinel
        engine.verifyContract(returnValue);
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
    vector<uint8_t> ret = parseHexString(name.substr(2, string::npos));
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
      { string("evm2wasm"), evm2wasmAddress }
    };

    if (aliases.count(name) == 0) {
      HERA_DEBUG << "Failed to resolve system contract alias: " << name << "\n";
      return false;
    }

    address = aliases.at(name);
  }

  string contents = loadFileContents(value);
  if (contents.size() == 0) {
    HERA_DEBUG << "Failed to load contract source (or empty): " << value << "\n";
    return false;
  }

  HERA_DEBUG << "Loaded contract for " << name << " from " << value << " (" << contents.size() << " bytes)\n";

  hera->contract_preload_list[address] = vector<uint8_t>(contents.begin(), contents.end());

  return true;
}

evmc_set_option_result hera_set_option(
  evmc_instance *instance,
  char const *name,
  char const *value
) noexcept {
  hera_instance* hera = static_cast<hera_instance*>(instance);

  if (strcmp(name, "evm1mode") == 0) {
    if (evm1mode_options.count(value)) {
      hera->evm1mode = evm1mode_options.at(value);
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strcmp(name, "metering") == 0) {
    hera->metering = strcmp(value, "true") == 0;
    return EVMC_SET_OPTION_SUCCESS;
  }

  if (strcmp(name, "engine") == 0) {
    auto it = wasm_engine_map.find(value);
    if (it != wasm_engine_map.end()) {
      hera->engine = it->second();
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

void hera_destroy(evmc_instance* instance) noexcept
{
  hera_instance* hera = static_cast<hera_instance*>(instance);
  delete hera;
}

evmc_capabilities_flagset hera_get_capabilities(evmc_instance* instance)
{
  evmc_capabilities_flagset caps = EVMC_CAPABILITY_EWASM;
  if (static_cast<hera_instance*>(instance)->evm1mode != hera_evm1mode::reject)
    caps |= EVMC_CAPABILITY_EVM1;
  return caps;
}

} // anonymous namespace

extern "C" {

evmc_instance* evmc_create_hera() noexcept
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
EVMC_EXPORT evmc_instance* evmc_create() noexcept
{
  return evmc_create_hera();
}
#endif

}
