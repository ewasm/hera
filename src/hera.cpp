/*
 * Hera VM: eWASM virtual machine conforming to the Ethereum VM C API
 *
 * Copyright (c) 2016 Alex Beregszaszi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <fstream>

#include <pass.h>
#include <wasm.h>
#include <wasm-binary.h>
#include <wasm-builder.h>
#include <wasm-interpreter.h>
#include <wasm-printing.h>
#include <wasm-validator.h>

#include <evmc/evmc.h>

#include "hera.h"
#include "eei.h"
#include "exceptions.h"
#include "vm.h"

using namespace std;
using namespace wasm;
using namespace HeraVM;

enum hera_evm_mode {
  EVM_FALLBACK,
  EVM2WASM,
  EVM2WASM_JS,
  EVM2WASM_JS_TRACING
};

struct hera_instance : evmc_instance {
  hera_evm_mode evm_mode;
  bool metering = false;
  wasm_vm vm = VM_BINARYEN;

  hera_instance() : evmc_instance({EVMC_ABI_VERSION, "hera", "0.0.0", nullptr, nullptr, nullptr}) {}
};

namespace {

bool hasWasmPreamble(vector<uint8_t> const& _input) {
  return
    _input.size() >= 8 &&
    _input[0] == 0 &&
    _input[1] == 'a' &&
    _input[2] == 's' &&
    _input[3] == 'm' &&
    _input[4] == 1 &&
    _input[5] == 0 &&
    _input[6] == 0 &&
    _input[7] == 0;
}

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

vector<uint8_t> sentinel(evmc_context* context, vector<uint8_t> const& input)
{
#if HERA_DEBUGGING
  cerr << "Metering (input " << input.size() << " bytes)..." << endl;
#endif

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  vector<uint8_t> ret = callSystemContract(
    context,
    { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa } }, // precompile address 0x00...0a
    gas,
    input
  );

#if HERA_DEBUGGING
  cerr << "Metering done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas)" << endl;
#endif

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

vector<uint8_t> evm2wasm_js(vector<uint8_t> const& input, bool evmTrace) {
#if HERA_DEBUGGING
  cerr << "Calling evm2wasm.js (input " << input.size() << " bytes)..." << endl;
#endif

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

#if HERA_DEBUGGING
  cerr << "(Calling evm2wasm.js with command: " << cmd << ")" << endl;
#endif

  int ret = system(cmd.data());
  unlink(fileEVM.data());

  if (ret != 0) {
#if HERA_DEBUGGING
    cerr << "evm2wasm.js failed" << endl;
#endif

    unlink(fileWASM.data());
    return vector<uint8_t>();
  }

  ifstream is(fileWASM);
  string str((istreambuf_iterator<char>(is)),
                 istreambuf_iterator<char>());

  unlink(fileWASM.data());

#if HERA_DEBUGGING
  cerr << "evm2wasm.js done (output " << str.length() << " bytes)" << endl;
#endif

  return vector<uint8_t>(str.begin(), str.end());
}

vector<uint8_t> evm2wasm(evmc_context* context, vector<uint8_t> const& input) {
#if HERA_DEBUGGING
  cerr << "Calling evm2wasm (input " << input.size() << " bytes)..." << endl;
#endif

  int64_t startgas = numeric_limits<int64_t>::max(); // do not charge for metering yet (give unlimited gas)
  int64_t gas = startgas;
  vector<uint8_t> ret = callSystemContract(
    context,
    { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xb } }, // precompile address 0x00...0b
    gas,
    input
  );

#if HERA_DEBUGGING
  cerr << "evm2wasm done (output " << ret.size() << " bytes, used " << (startgas - gas) << " gas)" << endl;
#endif

  return ret;
}

void hera_destroy_result(evmc_result const* result)
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
) {
  hera_instance* hera = static_cast<hera_instance*>(instance);

  evmc_result ret;
  memset(&ret, 0, sizeof(evmc_result));

  try {
    heraAssert(rev == EVMC_BYZANTIUM, "Only Byzantium supported.");
    heraAssert(msg->gas >= 0, "Negative startgas?");

    bool meterInterfaceGas = true;
    vector<uint8_t> _code(code, code + code_size);

    heraAssert(msg->gas >= 0, "Negative startgas?");
    // ensure we can only handle WebAssembly version 1
    if (!hasWasmPreamble(_code)) {
      switch (hera->evm_mode) {
      case EVM2WASM:
        _code = evm2wasm(context, _code);
        ensureCondition(_code.size() > 5, ContractValidationFailure, "Transcompiling via evm2wasm failed");
        meterInterfaceGas = false;
        break;
      case EVM2WASM_JS:
      case EVM2WASM_JS_TRACING:
        _code = evm2wasm_js(_code, hera->evm_mode == EVM2WASM_JS_TRACING);
        ensureCondition(_code.size() > 5, ContractValidationFailure, "Transcompiling via evm2wasm.js failed");
        meterInterfaceGas = false;
        break;
      case EVM_FALLBACK:
        ret.status_code = EVMC_REJECTED;
        return ret;
      default:
        ret.status_code = EVMC_FAILURE;
        return ret;
      }
    }

    if (msg->kind == EVMC_CREATE) {
      // Meter the deployment (constructor) code
      if (hera->metering)
        _code = sentinel(context, _code);
      ensureCondition(_code.size() > 5, ContractValidationFailure, "Invalid contract or metering failed.");
    }

    //execute(context, _code, *msg, result);
    ExecutionResult vmresult;
    
    /* This should probably be cleaned up a bit */
    switch (hera->vm) {
    #if WABT_SUPPORTED
    case VM_WABT:
      WabtVM vm = WabtVM(_code, *msg, context, meterInterfaceGas);
      vm.execute();
      vmresult = vm.getResult();
    #endif
    #if WAVM_SUPPORTED
    case VM_WAVM:
      WavmVM vm = WavmVM(_code, *msg, context, meterInterfaceGas);
      vm.execute();
      vmresult = vm.getResult();
    #endif
    default:
      BinaryenVM vm = BinaryenVM(_code, *msg, context, meterInterfaceGas);
      vm.execute();
      vmresult = vm.getResult();
    }

    // copy call result
    if (vmresult.returnValue.size() > 0) {
      vector<uint8_t> returnValue;

      if (msg->kind == EVMC_CREATE && !vmresult.isRevert && hasWasmPreamble(vmresult.returnValue)) {
        // Meter the deployed code
        returnValue = hera->metering ? sentinel(context, vmresult.returnValue) : move(vmresult.returnValue);

        ensureCondition(returnValue.size() > 5, ContractValidationFailure, "Invalid contract or metering failed.");
      } else {
        returnValue = move(vmresult.returnValue);
      }

      uint8_t* output_data = new uint8_t[returnValue.size()];
      copy(returnValue.begin(), returnValue.end(), output_data);

      ret.output_size = returnValue.size();
      ret.output_data = output_data;
      ret.release = hera_destroy_result;
    }

    ret.status_code = vmresult.isRevert ? EVMC_REVERT : EVMC_SUCCESS;
    ret.gas_left = vmresult.gasLeft;
  } catch (OutOfGasException const& e) {
    ret.status_code = EVMC_OUT_OF_GAS;
#if HERA_DEBUGGING
    cerr << e.what() << endl;
#endif
  } catch (ContractValidationFailure const& e) {
    ret.status_code = EVMC_CONTRACT_VALIDATION_FAILURE;
#if HERA_DEBUGGING
    cerr << e.what() << endl;
#endif
  } catch (InvalidMemoryAccess const& e) {
    ret.status_code = EVMC_INVALID_MEMORY_ACCESS;
#if HERA_DEBUGGING
    cerr << e.what() << endl;
#endif
  } catch (StaticModeViolation const& e) {
    ret.status_code = EVMC_STATIC_MODE_VIOLATION;
#if HERA_DEBUGGING
    cerr << e.what() << endl;
#endif
  } catch (InternalErrorException const& e) {
    ret.status_code = EVMC_INTERNAL_ERROR;

#if HERA_DEBUGGING
    cerr << "InternalError: " << e.what() << endl;
#endif
  } catch (exception const& e) {
    ret.status_code = EVMC_INTERNAL_ERROR;
#if HERA_DEBUGGING
    cerr << "Unknown exception: " << e.what() << endl;
#endif
  } catch (...) {
    ret.status_code = EVMC_INTERNAL_ERROR;
#if HERA_DEBUGGING
    cerr << "Totally unknown exception" << endl;
#endif
  }

  return ret;
}

int hera_set_option(
  evmc_instance *instance,
  char const *name,
  char const *value
) {

  hera_instance* hera = static_cast<hera_instance*>(instance);

  if (strcmp(name, "fallback") == 0) {
    if (strcmp(value, "true") == 0)
      hera->evm_mode = EVM_FALLBACK;
    return 1;
  }

  if (strcmp(name, "evm2wasm") == 0) {
    if (strcmp(value, "true") == 0)
      hera->evm_mode = EVM2WASM;
    return 1;
  }

  if (strcmp(name, "evm2wasm.js") == 0) {
    if (strcmp(value, "true") == 0)
      hera->evm_mode = EVM2WASM_JS;
    return 1;
  }

  if (strcmp(name, "evm2wasm.js-trace") == 0) {
    if (strcmp(value, "true") == 0)
      hera->evm_mode = EVM2WASM_JS_TRACING;
    return 1;
  }

  if (strcmp(name, "metering") == 0) {
    hera->metering = strcmp(value, "true") == 0;
    return 1;
  }
  if (strcmp(name, "vm") == 0) {
    if (strcmp(value, "binaryen") == 0)
      hera->vm = VM_BINARYEN;
#if WABT_SUPPORTED
    if (strcmp(value, "wabt") == 0)
      hera->vm = VM_WABT;
#endif
#if WAVM_SUPPORTED
    if (strcmp(value, "wavm") == 0)
      hera->vm = VM_WAVM;
#endif
    return 1;
  }
  return 0;
}

void hera_destroy(evmc_instance* instance)
{
  hera_instance* hera = static_cast<hera_instance*>(instance);
  delete hera;
}

} // anonymous namespace

extern "C" {

evmc_instance* evmc_create_hera()
{
  hera_instance* instance = new hera_instance;
  instance->destroy = hera_destroy;
  instance->execute = hera_execute;
  instance->set_option = hera_set_option;
  return instance;
}

}
