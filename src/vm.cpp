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

#include "vm.h"

using namespace std;
using namespace wasm;
using namespace HeraVM;

int BinaryenVM::execute()
{
  Module module;
  
  /* Parse WASM bytecode */
  try {
    WasmBinaryBuilder parser(module, reinterpret_cast<vector<char> const&>(this->code), false);
    parser.read();
  } catch (ParseException &p) {
    /* TODO: Potentially introduce abstracted VM exceptions */
    heraAssert(
      false, 
      "Error in parsing WASM binary: '" +
      p.text + 
      "' at " + 
      to_string(p.line) + 
      ":" + 
      to_string(p.col));
  }

  /* Validate WASM binary */
  ensureCondition(
    WasmValidator().validate(module),
    ContractValidationFailure, 
    "Module is not valid."
  );

  /* Ensure ewasm contract entry point exists */
  ensureCondition(
    module.getExportOrNull(Name("main")) != nullptr,
    ContractValidationFailure,
    "Contract entry point (\"main\") missing."
  );
  
  /* Instantiate EEI object */
  BinaryenEEI interface(context, code, state_code, msg, output, meterGas);
  
  ModuleInstance instance(module, &interface);
  
  /* Call 'main' symbol */
  Name main = Name("main");
  LiteralList args;
  instance.callExport(main, args);

  return 0;
}
