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

namespace {

vector<uint8_t> sentinel(struct evm_context* context, vector<uint8_t> const& input)
{
#if HERA_DEBUGGING
  cerr << "Metering (input " << input.size() << " bytes)..." << endl;
#endif

#if HERA_METERING_CONTRACT
  evm_message metering_message = {
    .destination = { .bytes = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa } }, // precompile address 0x00...0a
    .sender = {},
    .value = {},
    .input_data = input.data(),
    .input_size = input.size(),
    .code_hash = {},
    .gas = -1, // do not charge for metering yet (give unlimited gas)
    .depth = 0,
    .kind = EVM_CALL,
    .flags = EVM_STATIC
  };

  evm_result metering_result;
  context->fn_table->call(&metering_result, context, &metering_message);

  vector<uint8_t> ret;
  if (metering_result.status_code == EVM_SUCCESS && metering_result.output_data)
    ret.assign(metering_result.output_data, metering_result.output_data + metering_result.output_size);

  if (metering_result.release)
    metering_result.release(&metering_result);

#if HERA_DEBUGGING
  cerr << "Metering done (output " << ret.size() << " bytes)" << endl;
#endif

  return ret;
#else
  (void)context;
  return input;
#endif
}

void execute(
	struct evm_context* context,
	vector<uint8_t> & code,
	struct evm_message const& msg,
	ExecutionResult & result
) {
#if HERA_DEBUGGING
  cerr << "Executing..." << endl;
#endif

  Module module;

  // Load module
  try {
    // FIXME: should get rid of this horrible typecast
    WasmBinaryBuilder parser(module, reinterpret_cast<vector<char> &>(code), false);
    parser.read();
  } catch (ParseException &p) {
    throw InternalErrorException(
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

  // Optimise
  // PassRunner passRunner(module);
  // passRunner.addDefaultOptimizationPasses();
  // passRunner.run();

  // Interpet
  EthereumInterface interface(context, code, msg, result);
  ModuleInstance instance(module, &interface);

  Name main = Name("main");
  LiteralList args;
  instance.callExport(main, args);
}

}

extern "C" {

static void evm_destroy_result(struct evm_result const* result)
{
  if (result->output_data) {
     free((void *)result->output_data);
  }
}

static struct evm_result evm_execute(
  struct evm_instance* instance,
  struct evm_context* context,
  enum evm_revision rev,
  const struct evm_message* msg,
  const uint8_t* code,
  size_t code_size)
{
  struct evm_result ret;
  memset(&ret, 0, sizeof(struct evm_result));

  try {
    heraAssert(instance != NULL, "");
    heraAssert(msg->gas >= 0, "Negative startgas?");

    ExecutionResult result;
    result.gasLeft = (uint64_t)msg->gas;

    // ensure we can only handle WebAssembly version 1
    if (code_size < 5 || code[0] != 0 || code[1] != 'a' || code[2] != 's' || code[3] != 'm' || code[4] != 1) {
      ret.status_code = EVM_REJECTED;
      return ret;
    }

    heraAssert(rev == EVM_BYZANTIUM, "Only Byzantium supported.");

    vector<uint8_t> _code(code, code + code_size);

    if (msg->kind == EVM_CREATE) {
      // Meter the deployment (constructor) code
      _code = sentinel(context, _code);
      heraAssert(_code.size() > 5, "Invalid contract or metering failed.");
    }

    execute(context, _code, *msg, result);

    // copy call result
    if (result.returnValue.size() > 0) {
      vector<uint8_t> returnValue;

      if (msg->kind == EVM_CREATE && !result.isRevert) {
        // Meter the deployed code
        returnValue = sentinel(context, result.returnValue);
        heraAssert(returnValue.size() > 5, "Invalid contract or metering failed.");
      } else {
        returnValue = move(result.returnValue);
      }

      uint8_t* output_data = (uint8_t*)malloc(returnValue.size());
      heraAssert(output_data != NULL, "Memory allocation failure.");
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


static void evm_destroy(struct evm_instance* instance)
{
  free(instance);
}

struct evm_instance* hera_create()
{
  struct evm_instance init = {
    .abi_version = EVM_ABI_VERSION,
    .destroy = evm_destroy,
    .execute = evm_execute,
    .set_option = (evm_set_option_fn)NULL
  };
  struct evm_instance* instance = (struct evm_instance*)calloc(1, sizeof(struct evm_instance));
  if (instance)
    memcpy(instance, &init, sizeof(init));
  return instance;
}

}
