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
#include <shell-interface.h>

#include "evm.h"
#include "hera.h"
#include "eei.h"

using namespace wasm;
using namespace HeraVM;

extern "C" {

char const* evm_get_info(enum evm_info_key key)
{
  switch(key) {
    case EVM_NAME: return "Hera (eWASM)"; break;
    case EVM_VERSION: return "git"; break;
  }

  return "";
}


struct evm_instance* evm_create(evm_query_fn query_fn,
                                       evm_update_fn update_fn,
                                       evm_call_fn call_fn)
{
  Hera *hera = new Hera(query_fn, update_fn, call_fn);

  return reinterpret_cast<evm_instance*>(hera);
}

void evm_destroy(struct evm_instance* instance)
{
  Hera *hera = reinterpret_cast<Hera*>(instance);
  delete hera;
}

bool evm_set_option(struct evm_instance* evm,
                           char const* name,
                           char const* value)
{
  return false;
}

struct evm_result evm_execute(struct evm_instance* instance,
                                     struct evm_env* env,
                                     enum evm_mode mode,
                                     struct evm_hash256 code_hash,
                                     uint8_t const* code,
                                     size_t code_size,
                                     int64_t gas,
                                     uint8_t const* input,
                                     size_t input_size,
                                     struct evm_uint256 value)
{
  auto hera = *reinterpret_cast<Hera*>(instance);
  struct evm_result ret;

  memset(&ret, 0, sizeof(struct evm_result));

  std::vector<char> _code(false);
  _code.resize(code_size);
  std::copy_n(code, code_size, _code.begin());

  std::vector<char> _input(false);
  if (input_size) {
    _input.resize(input_size);
    std::copy_n(input, input_size, _input.begin());
  }

  HeraCall *call = new HeraCall(env, _code, gas, _input, value);

  try {
    hera.execute(call);
  } catch (std::exception &e) {
    // FIXME: `evm_result` should have a way to report this back
    Fatal() << "Execution failed: " << e.what() << "\n";
  }

  // copy call result
  ret.output_size = call->returnValue.size();
  ret.output_data = (const uint8_t *)malloc(ret.output_size);
  // FIXME: properly handle memory allocation issues
  if (ret.output_data) {
    std::copy(call->returnValue.begin(), call->returnValue.end(), (char *)ret.output_data);
  }
  ret.gas_left = call->gas;

  delete call;

  return ret;
}

void evm_destroy_result(struct evm_result result)
{
  if (result.output_data) {
     free((void *)result.output_data);
  }
}

}

void Hera::execute(HeraCall *call) {
  std::cout << "Executing...\n";

  Module* module = new Module();

  // Load module
  try {
    WasmBinaryBuilder parser(*module, call->code, false);
    parser.read();
  } catch (ParseException &p) {
    throw std::invalid_argument("Error in parsing WASM binary: '" +
      p.text + "' at " + std::to_string(p.line) + ":" + std::to_string(p.col));
  }

  // Print
  // WasmPrinter::printModule(module);

  // Validate
  std::cout << "Validated: " << WasmValidator().validate(*module) << "\n";

  // Optimise
  // PassRunner passRunner(module);
  // passRunner.addDefaultOptimizationPasses();
  // passRunner.run();

  // Interpet
  EthereumInterface *interface = new EthereumInterface(this, call);
  ModuleInstance instance(*module, interface);

  Name main = Name("main");
  LiteralList args;
  instance.callExport(main, args);

  delete interface;
  delete module;
}
