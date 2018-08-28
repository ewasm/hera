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

#include <vector>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <iomanip>

#include <evmc/evmc.h>

#include <evm2wasm.h>

#include "binaryen.h"
#include "debugging.h"
#include "eei.h"
#include "exceptions.h"
#include "helpers.h"
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
  evm2wasm_cpp,
  evm2wasm_cpp_tracing,
  evm2wasm_js,
  evm2wasm_js_tracing
};

using WasmEngineCreateFn = std::unique_ptr<WasmEngine>(*)();

const map<string, WasmEngineCreateFn> wasm_engine_map {
  { "binaryen", BinaryenEngine::create },
#if HERA_WAVM
  { "wavm", []{ return std::unique_ptr<WasmEngine>{}; } },
#endif
#if HERA_WABT
  { "wabt", WabtEngine::create },
#endif
};

const map<string, hera_evm1mode> evm1mode_options {
  { "reject", hera_evm1mode::reject },
  { "fallback", hera_evm1mode::fallback },
  { "evm2wasm", hera_evm1mode::evm2wasm_contract },
  { "evm2wasm.cpp", hera_evm1mode::evm2wasm_cpp },
  { "evm2wasm.cpp-trace", hera_evm1mode::evm2wasm_cpp_tracing },
  { "evm2wasm.js", hera_evm1mode::evm2wasm_js },
  { "evm2wasm.js-trace", hera_evm1mode::evm2wasm_js_tracing },
};

struct hera_instance : evmc_instance {
  std::unique_ptr<WasmEngine> engine{new BinaryenEngine};
  hera_evm1mode evm1mode = hera_evm1mode::reject;
  bool metering = false;
  vector<pair<evmc_address, vector<uint8_t>>> contract_preload_list;

  hera_instance() noexcept : evmc_instance({EVMC_ABI_VERSION, "hera", hera_get_buildinfo()->project_version, nullptr, nullptr, nullptr, nullptr}) {}
};

const evmc_address sentinelAddress = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa } };
const evmc_address evm2wasmAddress = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb } };

// Checks if the contract preload list contains the given address.
vector<uint8_t> resolveSystemContract(hera_instance const* hera, evmc_address const& addr) {
  auto const& list = hera->contract_preload_list;

  for (size_t i = 0; i < list.size(); ++i) {
    if (memcmp(list[i].first.bytes, addr.bytes, sizeof(evmc_address)) == 0)
      return list[i].second;
  }

  return vector<uint8_t>{};
}

// Calls a system contract at @address with input data @input.
// It is a "staticcall" with sender 000...000 and no value.
// @returns output data from the contract and update the @gas variable with the gas left.
vector<uint8_t> callSystemContract(
  evmc_context* context,
  evmc_address const& address,
  int64_t & gas,
  vector<uint8_t> const& input
) {
  evmc_message message = {
    .destination = address,
    .sender = {},
    .value = {},
    .input_data = input.data(),
    .input_size = input.size(),
    .code_hash = {},
    .create2_salt = {},
    .gas = gas,
    .depth = 0,
    .kind = EVMC_CALL,
    .flags = EVMC_STATIC
  };

  evmc_result result;
  context->fn_table->call(&result, context, &message);

  vector<uint8_t> ret;
  if (result.status_code == EVMC_SUCCESS && result.output_data)
    ret.assign(result.output_data, result.output_data + result.output_size);

  gas = result.gas_left;

  if (result.release)
    result.release(&result);

  return ret;
}

// Calls the Sentinel contract with input data @input.
// @returns the validated and metered output or empty output otherwise.
vector<uint8_t> sentinel(evmc_context* context, vector<uint8_t> const& input)
{
  HERA_DEBUG << "Metering (input " << input.size() << " bytes)...\n";

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  vector<uint8_t> ret = callSystemContract(
    context,
    sentinelAddress,
    gas,
    input
  );

  HERA_DEBUG << "Metering done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas)\n";

  return ret;
}

// NOTE: assumes that pattern doesn't contain any formatting characters (e.g. %)
string mktemp_string(string pattern) {
  const unsigned long len = pattern.size();
  char tmp[len + 1];
  strcpy(tmp, pattern.data());
  if (!mktemp(tmp) || (tmp[0] == 0))
     return string();
  return string(tmp, strlen(tmp));
}

// Calls evm2wasm (as a Javascript CLI) with input data @input.
// @returns the compiled output or empty output otherwise.
vector<uint8_t> evm2wasm_js(vector<uint8_t> const& input, bool evmTrace) {
  HERA_DEBUG << "Calling evm2wasm.js (input " << input.size() << " bytes)...\n";

  string fileEVM = mktemp_string("/tmp/hera.evm2wasm.evm.XXXXXX");
  string fileWASM = mktemp_string("/tmp/hera.evm2wasm.wasm.XXXXXX");

  if (fileEVM.size() == 0 || fileWASM.size() == 0)
    return vector<uint8_t>();

  ofstream os;
  os.open(fileEVM);
  // print as a hex sting
  os << hex;
  for (uint8_t byte: input)
    os << setfill('0') << setw(2) << static_cast<int>(byte);
  os.close();

  string cmd = string("evm2wasm.js ") + "-e " + fileEVM + " -o " + fileWASM + " --charge-per-op";
  if (evmTrace)
    cmd += " --trace";

  HERA_DEBUG << "(Calling evm2wasm.js with command: " << cmd << ")\n";

  int ret = system(cmd.data());
  unlink(fileEVM.data());

  if (ret != 0) {
    HERA_DEBUG << "evm2wasm.js failed\n";

    unlink(fileWASM.data());
    return vector<uint8_t>();
  }

  string str = loadFileContents(fileWASM);

  unlink(fileWASM.data());

  HERA_DEBUG << "evm2wasm.js done (output " << str.length() << " bytes)\n";

  return vector<uint8_t>(str.begin(), str.end());
}

// Calls evm2wasm (through the built-in C++ interface) with input data @input.
// @returns the compiled output or empty output otherwise.
vector<uint8_t> evm2wasm_cpp(vector<uint8_t> const& input, bool evmTrace) {
  HERA_DEBUG << "Calling evm2wasm.cpp (input " << input.size() << " bytes)...\n";

  string str = evm2wasm::evm2wasm(input, evmTrace);

  HERA_DEBUG << "evm2wasm.cpp done (output " << str.length() << " bytes)\n";

  return vector<uint8_t>(str.begin(), str.end());
}

// Calls the evm2wasm contract with input data @input.
// @returns the compiled output or empty output otherwise.
vector<uint8_t> evm2wasm(evmc_context* context, vector<uint8_t> const& input) {
  HERA_DEBUG << "Calling evm2wasm (input " << input.size() << " bytes)...\n";

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  vector<uint8_t> ret = callSystemContract(
    context,
    evm2wasmAddress,
    gas,
    input
  );

  HERA_DEBUG << "evm2wasm done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas)\n";

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

    vector<uint8_t> override_code = resolveSystemContract(hera, msg->destination);
    if (override_code.size() > 0) {
      HERA_DEBUG << "Overriding contract.\n";
      run_code = std::move(override_code);
    }

    // ensure we can only handle WebAssembly version 1
    if (!hasWasmPreamble(run_code)) {
      switch (hera->evm1mode) {
      case hera_evm1mode::evm2wasm_contract:
        run_code = evm2wasm(context, run_code);
        ensureCondition(run_code.size() > 5, ContractValidationFailure, "Transcompiling via evm2wasm failed");
        // TODO: enable this once evm2wasm does metering of interfaces
        // meterInterfaceGas = false;
        break;
      case hera_evm1mode::evm2wasm_cpp:
      case hera_evm1mode::evm2wasm_cpp_tracing:
        run_code = evm2wasm_cpp(run_code, hera->evm1mode == hera_evm1mode::evm2wasm_cpp_tracing);
        ensureCondition(run_code.size() > 5, ContractValidationFailure, "Transcompiling via evm2wasm.cpp failed");
        // TODO: enable this once evm2wasm does metering of interfaces
        // meterInterfaceGas = false;
        break;
      case hera_evm1mode::evm2wasm_js:
      case hera_evm1mode::evm2wasm_js_tracing:
        run_code = evm2wasm_js(run_code, hera->evm1mode == hera_evm1mode::evm2wasm_js_tracing);
        ensureCondition(run_code.size() > 5, ContractValidationFailure, "Transcompiling via evm2wasm.js failed");
        // TODO: enable this once evm2wasm does metering of interfaces
        // meterInterfaceGas = false;
        break;
      case hera_evm1mode::fallback:
        HERA_DEBUG << "Non-WebAssembly input, but fallback mode enabled, asking client to deal with it.\n";
        ret.status_code = EVMC_REJECTED;
        return ret;
      case hera_evm1mode::reject:
        HERA_DEBUG << "Non-WebAssembly input, failure.n\n";
        ret.status_code = EVMC_FAILURE;
        return ret;
      }
    } else if (msg->kind == EVMC_CREATE) {
      // Meter the deployment (constructor) code if it is WebAssembly
      if (hera->metering)
        run_code = sentinel(context, run_code);
      ensureCondition(run_code.size() > 5, ContractValidationFailure, "Invalid contract or metering failed.");
    }

    heraAssert(hera->engine, "Wasm engine not set.");
    WasmEngine& engine = *hera->engine;

    ExecutionResult result = engine.execute(context, run_code, state_code, *msg, meterInterfaceGas);
    heraAssert(result.gasLeft >= 0, "Negative gas left after execution.");

    // copy call result
    if (result.returnValue.size() > 0) {
      vector<uint8_t> returnValue;

      if (msg->kind == EVMC_CREATE && !result.isRevert && hasWasmPreamble(result.returnValue)) {
        // Meter the deployed code if it is WebAssembly
        returnValue = hera->metering ? sentinel(context, result.returnValue) : move(result.returnValue);
        ensureCondition(returnValue.size() > 5, ContractValidationFailure, "Invalid contract or metering failed.");
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

  hera->contract_preload_list.push_back(pair<evmc_address, vector<uint8_t>>(address, vector<uint8_t>(contents.begin(), contents.end())));

  return true;
}

int hera_set_option(
  evmc_instance *instance,
  char const *name,
  char const *value
) noexcept {
  hera_instance* hera = static_cast<hera_instance*>(instance);

  if (strcmp(name, "evm1mode") == 0) {
    if (evm1mode_options.count(value)) {
      hera->evm1mode = evm1mode_options.at(value);
      return 1;
    }
  }

  if (strcmp(name, "metering") == 0) {
    hera->metering = strcmp(value, "true") == 0;
    return 1;
  }

  if (strcmp(name, "engine") == 0) {
    auto it = wasm_engine_map.find(value);
    if (it != wasm_engine_map.end()) {
      hera->engine = it->second();
      return 1;
    }
  }

  if (strncmp(name, "sys:", 4) == 0) {
    if (hera_parse_sys_option(hera, string(name), string(value)))
      return 1;
  }

  return 0;
}

void hera_destroy(evmc_instance* instance) noexcept
{
  hera_instance* hera = static_cast<hera_instance*>(instance);
  delete hera;
}

} // anonymous namespace

extern "C" {

evmc_instance* evmc_create_hera() noexcept
{
  hera_instance* instance = new hera_instance;
  instance->destroy = hera_destroy;
  instance->execute = hera_execute;
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
