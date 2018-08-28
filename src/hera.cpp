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
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <fstream>

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

enum class hera_wasm_engine {
  binaryen,
  wavm,
  wabt
};

enum class hera_evm1mode {
  reject,
  fallback,
  evm2wasm_contract,
  evm2wasm_cpp,
  evm2wasm_cpp_tracing,
  evm2wasm_js,
  evm2wasm_js_tracing
};

const map<string, hera_wasm_engine> wasm_engine_options {
  { "binaryen", hera_wasm_engine::binaryen },
#if HERA_WAVM
  { "wavm", hera_wasm_engine::wavm },
#endif
#if HERA_WABT
  { "wabt", hera_wasm_engine::wabt },
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
  vector<pair<evmc_address, string>> contract_preload_list;

  hera_instance() noexcept : evmc_instance({EVMC_ABI_VERSION, "hera", hera_get_buildinfo()->project_version, nullptr, nullptr, nullptr, nullptr}) {}
};

int hera_create_wasm_engine(struct hera_instance *hera, hera_wasm_engine engine)
{
  switch (engine) {
  case hera_wasm_engine::binaryen:
    hera->engine.reset(new BinaryenEngine);
    break;
#if HERA_WABT
  case hera_wasm_engine::wabt:
    hera->engine.reset(new WabtEngine);
    break;
#endif
  default:
    return 0;
  }
  return 1;
}

const evmc_address sentinelAddress = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa } };
const evmc_address evm2wasmAddress = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb } };

// Resolve an address on the preload list to a filepath containing the binary.
// This assumes that the address is on the list, implying resolveSystemContract has been called.
string resolvePreloadPath(const evmc_address* addr, const hera_instance *hera) {
  auto const& list = hera->contract_preload_list;

  for (size_t i = 0; i < list.size(); ++i) {
    if (memcmp(list[i].first.bytes, addr->bytes, sizeof(evmc_address)) == 0) {
      HERA_DEBUG << "Successfully resolved address " << bytesAsHexStr(addr->bytes, 20) << " to filepath " << list[i].second << "\n";
      return string(list[i].second);
    }
  }

  heraAssert(false, "The specified address could not be resolved to a filepath with its binary.");
}

// Returns the bytecode to be overridden before execution
vector<uint8_t> overrideRunCode(const evmc_address *addr, const hera_instance *hera) {
  const string path = resolvePreloadPath(addr, hera);

  HERA_DEBUG << "Attempting to load file " << path << "\n";
  string ret = loadFileContents(path);
  HERA_DEBUG << "Successfully loaded file " << path << "\n";

  return vector<uint8_t>(ret.begin(), ret.end());
}

// Checks if the contract preload list contains the given address.
bool resolveSystemContract(const hera_instance *hera, const evmc_address *addr) {
  auto const& list = hera->contract_preload_list;

  for (size_t i = 0; i < list.size(); ++i) {
    if (memcmp(list[i].first.bytes, addr->bytes, sizeof(evmc_address)) == 0) {
      HERA_DEBUG << "Successfully resolved address " << bytesAsHexStr(addr->bytes, 20) << "\n";
      return true;
    }
  }

  HERA_DEBUG << "Address does not match " << bytesAsHexStr(addr->bytes, 20) << "\n";
  return false;
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

    if (resolveSystemContract(hera, &msg->destination)) {
      HERA_DEBUG << "Overriding contract\n";
      run_code = overrideRunCode(&msg->destination, hera);
      run_code.shrink_to_fit();
    }

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
    // TODO: use specific error code? EVMC_ARGUMENT_OUT_OF_RANGE?
    ret.status_code = EVMC_FAILURE;
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

// TODO: use C++17 and std::optional
pair<evmc_address, bool> resolve_alias_to_address(string const& alias) {
  const map<string, evmc_address> alias_to_addr_map = {
    { string("sentinel"), sentinelAddress },
    { string("evm2wasm"), evm2wasmAddress }
  };

  if (alias_to_addr_map.count(alias) != 0) {
    return { alias_to_addr_map.at(alias), true };
  }

  return { evmc_address{}, false };
}

pair<evmc_address, bool> parse_hex_addr(string const& addr) {
  evmc_address ret = {};

  HERA_DEBUG << "Trying to parse address field\n";

  if (addr.find("0x") != 0) { 
    heraAssert(false, "Address missing '0x' prefix!");
  }

  heraAssert(addr.size() <= 42, "Address specified is too long!");

  string addr_raw;
  // If the number of nibbles is odd, we must prepend a zero for unmarshalling to work correctly.
  if (addr.size() % 2 > 0) addr_raw.push_back('0');
  addr_raw.append(addr.substr(2, string::npos));

  size_t hex_length = addr_raw.size();

  HERA_DEBUG << "Got hex string of length " << hex_length << ": " << addr_raw << "\n";

  // Use strtol to parse hex string into binary
  for (size_t i = hex_length / 2, j = 20; i > 0 && j > 0; i--, j--) {
    string byte_str = addr_raw.substr(((i - 1) * 2), 2);

    uint8_t byte = uint8_t(strtol(byte_str.c_str(), nullptr, 16));

    ret.bytes[j - 1] = byte;
  }

  HERA_DEBUG << "Successfully unmarshalled hex string into address struct\n";

  return pair<evmc_address, bool>(ret, true);
}

pair<evmc_address, bool> parse_preload_addr(const char *name)
{
  assert(name != nullptr);

  pair<evmc_address, bool> ret = { {}, false };
  string evmc_option_raw = string(name);

  HERA_DEBUG << "Trying to parse EVMC option as preload flag: " << evmc_option_raw << "\n";

  // Check the "sys:" syntax by comparing substring
  if (evmc_option_raw.find("sys:") != 0) {
    HERA_DEBUG << "Unsuccessfully parsed preload command, prefix malformed: " << evmc_option_raw.substr(0, 4) << "\n";
    return ret;
  }

  // Parse the address field from the option name and try to determine an address
  string opt_address_to_load = evmc_option_raw.substr(4, string::npos);

  // Try to resolve the substring to an alias first
  HERA_DEBUG << "Attempting to parse option as an alias: " << opt_address_to_load << "\n";
  ret = resolve_alias_to_address(opt_address_to_load);

  // If alias resolver returns false, try parsing to a hex address
  if (ret.second == false) {
    HERA_DEBUG << "Unsuccessfully resolved option to an alias, trying to unmarshal from a hex string\n";
    ret = parse_hex_addr(opt_address_to_load);
  }

  return ret;
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
    auto it = wasm_engine_options.find(value);
    if (it != wasm_engine_options.end()) {
      return hera_create_wasm_engine(hera, it->second);
    }
  }

  auto preload_addr = parse_preload_addr(name);
  if (preload_addr.second == true) {
    hera->contract_preload_list.push_back(pair<evmc_address, string>(preload_addr.first, string(value)));
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
