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

#if WAVM_SUPPORTED
#include <IR/Module.h>
#include <IR/Validate.h>
#include <WASM/WASM.h>
#include <Runtime/Runtime.h>
#include <Runtime/Linker.h>
#include <Runtime/Intrinsics.h>
#endif

#include "vm.h"

using namespace std;
using namespace HeraVM;

int BinaryenVM::execute()
{
  wasm::Module module;
  
  //Copy over bytecode instead of type-punning to avoid breaking alias rules
  vector<char> const bytecode(code.begin(), code.end());
  
  /* Parse WASM bytecode */
  try {
    wasm::WasmBinaryBuilder parser(module, bytecode, false);
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
  
  validate_contract(module);

  /* Instantiate EEI object */
  BinaryenEEI interface(context, code, state_code, msg, output, meterGas);
  
  wasm::ModuleInstance instance(module, &interface);
  
  /* Call 'main' symbol */
  wasm::Name main = Name("main");
  wasm::LiteralList args;
  instance.callExport(main, args);

  return 0;
}

// NOTE: This should be caught during deployment time by the Sentinel.
void BinaryenVM::validate_contract(Module & module)
{
  ensureCondition(
    module.getExportOrNull(Name("main")) != nullptr,
    ContractValidationFailure,
    "Contract entry point (\"main\") missing."
  );

  ensureCondition(
    module.getExportOrNull(Name("memory")) != nullptr,
    ContractValidationFailure,
    "Contract export (\"memory\") missing."
  );

  ensureCondition(
    module.exports.size() == 2,
    ContractValidationFailure,
    "Contract exports more than (\"main\") and (\"memory\")."
  );

  for (auto const& import: module.imports) {
    ensureCondition(
      import->module == Name("ethereum")
#if HERA_DEBUGGING
      || import->module == Name("debug")
#endif
      ,
      ContractValidationFailure,
      "Import from invalid namespace."
    );
  }
}
#if WAVM_SUPPORTED
using namespace Serialization;
using namespace IR;
using namespace WASM;
using namespace Runtime;

struct importResolver : Resolver {
  importResolver(Compartment *_compartment): compartment(_compartment) { }

  Compartment *compartment;
  HashMap<std::string, ModuleInstance*> moduleNameToInstanceMap;

  bool resolve(const std::string& moduleName, const std::string& exportName, ObjectType type, Object*& outObject) override
  {
    auto namedInstance = moduleNameToInstanceMap.get(ModuleName);

    if (namedInstance) {
      if (outObject = getInstanceExport(*namedInstance, exportName)) {
        if (isA(outObject, type)) return true;
	else {
	  std::cout << "Resolved import of incorrect type" << endl;
	  return false;
	}
      }
    }

    return false;
  }
}

int WavmVM::execute() {
  DEFINE_INTRINSIC_MODULE(ethereum);
  // WAVM EEI function definitions go here

  return 0;
}
#endif
