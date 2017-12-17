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

EXPORT char const* evm_get_info(enum evm_info_key key)
{
  switch(key) {
    case EVM_NAME: return "Hera (eWASM)"; break;
    case EVM_VERSION: return "git"; break;
  }

  return "";
}


EXPORT struct evm_instance* evm_create(struct evm_host host)
{
  Hera *hera = new Hera(host);

  return reinterpret_cast<evm_instance*>(hera);
}

EXPORT void evm_destroy(struct evm_instance* instance)
{
  Hera *hera = reinterpret_cast<Hera*>(instance);
  delete hera;
}

EXPORT bool evm_set_option(struct evm_instance* evm,
                    char const* name,
                    char const* value)
{
  return false;
}

EXPORT struct evm_result evm_execute(struct evm_instance *instance,
                              struct evm_context *context,
                              enum evm_revision rev,
			      const struct evm_message *msg,
                              uint8_t const* code,
                              size_t code_size)
{
  auto hera = *reinterpret_cast<Hera*>(instance);
  struct evm_result ret;

  memset(&ret, 0, sizeof(struct evm_result));

  std::vector<char> _code(false);
  _code.resize(code_size);
  std::copy_n(code, code_size, _code.begin());

  std::vector<char> _input(false);
  if (msg->input_size) {
    _input.resize(msg->input_size);
    std::copy_n(msg->input, msg->input_size, _input.begin());
  }
  HeraCall *call = new HeraCall(context, _code, msg->gas, _input, value);

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

EXPORT void evm_destroy_result(struct evm_result result)
{
  if (result.output_data) {
     free((void *)result.output_data);
  }
}

EXPORT bool evmjit_is_code_ready(struct evm_instance* instance, enum evm_mode mode,
                                 struct evm_hash256 code_hash)
{
  return true;
}

EXPORT void evmjit_compile(struct evm_instance* instance, enum evm_mode mode,
                           uint8_t const* code, size_t code_size,
                           struct evm_hash256 code_hash)
{
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

  // Interpret
  EthereumInterface *interface = new EthereumInterface(this, call);
  ModuleInstance instance(*module, interface);

  Name main = Name("main");
  LiteralList args;
  instance.callExport(main, args);

  delete interface;
  delete module;
}
