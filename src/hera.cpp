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

#include "evm.h"
#include "hera.h"
#include "eei.h"

using namespace std;
using namespace wasm;
using namespace HeraVM;

struct hera_instance : evm_instance {
  bool fallback = false;
#if HERA_EVM2WASM
  bool use_evm2wasm_js = false;
#endif

  hera_instance() : evm_instance({EVM_ABI_VERSION, nullptr, nullptr, nullptr}) {}
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

#if HERA_METERING_CONTRACT || HERA_EVM2WASM
vector<uint8_t> callSystemContract(
  evm_context* context,
  evm_address const& address,
  int64_t & gas,
  vector<uint8_t> const& input
) {
  evm_message message = {
    .destination = address,
    .sender = {},
    .value = {},
    .input_data = input.data(),
    .input_size = input.size(),
    .code_hash = {},
    .gas = gas,
    .depth = 0,
    .kind = EVM_CALL,
    .flags = EVM_STATIC
  };

  evm_result result;
  context->fn_table->call(&result, context, &message);

  vector<uint8_t> ret;
  if (result.status_code == EVM_SUCCESS && result.output_data)
    ret.assign(result.output_data, result.output_data + result.output_size);

  gas = result.gas_left;

  if (result.release)
    result.release(&result);

  return ret;
}
#endif

vector<uint8_t> sentinel(evm_context* context, vector<uint8_t> const& input)
{
#if HERA_DEBUGGING
  cerr << "Metering (input " << input.size() << " bytes)..." << endl;
#endif

#if HERA_METERING_CONTRACT
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
#else
  (void)context;
  return input;
#endif
}

#if HERA_EVM2WASM
// NOTE: assumes that pattern doesn't contain any formatting characters (e.g. %)
string mktemp_string(string pattern) {
  const unsigned long len = pattern.size();
  char tmp[len];
  strncpy(tmp, pattern.data(), len);
  if (!mktemp(tmp))
     return string();
  return string(tmp, len);
}

vector<uint8_t> evm2wasm_js(vector<uint8_t> const& input) {
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

  string cmd = string("evm2wasm.js ") + "-e " + fileEVM + " -o " + fileWASM;
  int ret = system(cmd.data());
  unlink(fileEVM.data());

  if (ret != 0) {
    unlink(fileWASM.data());
    return vector<uint8_t>();
  }

  ifstream is(fileWASM);
  string str((istreambuf_iterator<char>(is)),
                 istreambuf_iterator<char>());

  unlink(fileWASM.data());

  return vector<uint8_t>(str.begin(), str.end());
}

vector<uint8_t> evm2wasm(evm_context* context, vector<uint8_t> const& input) {
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
#endif

void execute(
	evm_context* context,
	vector<uint8_t> const& code,
	evm_message const& msg,
	ExecutionResult & result
) {
#if HERA_DEBUGGING
  cerr << "Executing..." << endl;
#endif

  Module module;

  // Load module
  try {
    WasmBinaryBuilder parser(module, reinterpret_cast<vector<char> const&>(code), false);
    parser.read();
  } catch (ParseException &p) {
    heraAssert(
      false,
      "Error in parsing WASM binary: '" +
      p.text +
      "' at " +
      to_string(p.line) +
      ":" +
      to_string(p.col)
    );
  }

  // Print
  // WasmPrinter::printModule(module);

  // Validate
  heraAssert(WasmValidator().validate(module), "Module is not valid.");

  // This should be caught during deployment time by the Sentinel.
  // TODO: validate for other conditions too?
  heraAssert(module.getExportOrNull(Name("main")) != nullptr, "Contract entry point (\"main\") missing.");

  // NOTE: DO NOT use the optimiser here, it will conflict with metering

  // Interpet
  EthereumInterface interface(context, code, msg, result);
  ModuleInstance instance(module, &interface);

  Name main = Name("main");
  LiteralList args;
  instance.callExport(main, args);
}

void evm_destroy_result(evm_result const* result)
{
  delete[] result->output_data;
}

evm_result evm_execute(
  evm_instance* instance,
  evm_context* context,
  enum evm_revision rev,
  const evm_message* msg,
  const uint8_t* code,
  size_t code_size)
{
  evm_result ret;
  memset(&ret, 0, sizeof(evm_result));

  try {
    heraAssert(msg->gas >= 0, "Negative startgas?");

    ExecutionResult result;
    result.gasLeft = (uint64_t)msg->gas;

    vector<uint8_t> _code(code, code + code_size);

    // ensure we can only handle WebAssembly version 1
    if (!hasWasmPreamble(_code)) {
      hera_instance* hera = static_cast<hera_instance*>(instance);
#if HERA_EVM2WASM
      // Translate EVM bytecode to WASM
      if (hera->use_evm2wasm_js)
        _code = evm2wasm_js(_code);
      else
        _code = evm2wasm(context, _code);
      heraAssert(_code.size() != 0, "Transcompiling via evm2wasm failed");
#else
      ret.status_code = hera->fallback ? EVM_REJECTED : EVM_FAILURE;
      return ret;
#endif
    }

    heraAssert(rev == EVM_BYZANTIUM, "Only Byzantium supported.");

    if (msg->kind == EVM_CREATE) {
      // Meter the deployment (constructor) code
      _code = sentinel(context, _code);
      heraAssert(_code.size() > 5, "Invalid contract or metering failed.");
    }

    execute(context, _code, *msg, result);

    // copy call result
    if (result.returnValue.size() > 0) {
      vector<uint8_t> returnValue;

      if (msg->kind == EVM_CREATE && !result.isRevert && hasWasmPreamble(result.returnValue)) {
        // Meter the deployed code
        returnValue = sentinel(context, result.returnValue);
        heraAssert(returnValue.size() > 5, "Invalid contract or metering failed.");
      } else {
        returnValue = move(result.returnValue);
      }

      uint8_t* output_data = new uint8_t[returnValue.size()];
      copy(returnValue.begin(), returnValue.end(), output_data);

      ret.output_size = returnValue.size();
      ret.output_data = output_data;
      ret.release = evm_destroy_result;
    }

    ret.status_code = result.isRevert ? EVM_REVERT : EVM_SUCCESS;
    ret.gas_left = result.gasLeft;
  } catch (OutOfGasException) {
    ret.status_code = EVM_OUT_OF_GAS;
  } catch (InternalErrorException &e) {
    ret.status_code = EVM_INTERNAL_ERROR;
#if HERA_DEBUGGING
    cerr << "InternalError: " << e.what() << endl;
#endif
  } catch (exception &e) {
    ret.status_code = EVM_INTERNAL_ERROR;
#if HERA_DEBUGGING
    cerr << "Unknown exception: " << e.what() << endl;
#endif
  } catch (...) {
    ret.status_code = EVM_INTERNAL_ERROR;
#if HERA_DEBUGGING
    cerr << "Totally unknown exception" << endl;
#endif
  }

  return ret;
}

int evm_set_option(
  evm_instance* instance,
  char const* name,
  char const* value
) {
  hera_instance* hera = static_cast<hera_instance*>(instance);
  if (strcmp(name, "fallback") == 0) {
    hera->fallback = strcmp(value, "true") == 0;
    return 1;
  }
#if HERA_EVM2WASM
  if (strcmp(name, "evm2wasm.js") == 0) {
    hera->use_evm2wasm_js = strcmp(value, "true") == 0;
    return 1;
  }
#endif
  return 0;
}

void evm_destroy(evm_instance* instance)
{
  hera_instance* hera = static_cast<hera_instance*>(instance);
  delete hera;
}

} // anonymous namespace

extern "C" {

evm_instance* hera_create()
{
  hera_instance* instance = new hera_instance;
  instance->destroy = evm_destroy;
  instance->execute = evm_execute;
  instance->set_option = evm_set_option;
  return static_cast<evm_instance*>(instance);
}

}
