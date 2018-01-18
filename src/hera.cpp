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

void execute(
	struct evm_context* context,
	vector<uint8_t> & code,
	struct evm_message const& msg,
	ExecutionResult & result
) {
#if HERA_DEBUGGING
  cout << "Executing..." << endl;
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
      ret.status_code = EVM_UNSUPPORTED_CODE_TYPE;
      return ret;
    }

    heraAssert(rev == EVM_BYZANTIUM, "Only Byzantium supported.");

    vector<uint8_t> _code(code, code + code_size);
    execute(context, _code, *msg, result);

    // copy call result
    if (result.returnValue.size() > 0) {
      uint8_t* output_data = (uint8_t*)malloc(result.returnValue.size());
      heraAssert(output_data != NULL, "Memory allocation failure.");
      copy(result.returnValue.begin(), result.returnValue.end(), output_data);

      ret.output_size = result.returnValue.size();
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
    .get_code_status = (evm_get_code_status_fn)NULL,
    .prepare_code = (evm_prepare_code_fn)NULL,
    .set_option = (evm_set_option_fn)NULL
  };
  struct evm_instance* instance = (struct evm_instance*)calloc(1, sizeof(struct evm_instance));
  if (instance)
    memcpy(instance, &init, sizeof(init));
  return instance;
}

}
