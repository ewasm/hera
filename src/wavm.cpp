/*
 * Copyright 2018 Paul Dworzanski
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

#include "wavm.h"
#include "debugging.h"
#include "eei.h"
#include "exceptions.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

using namespace std;

namespace hera {

unique_ptr<WasmEngine> WavmEngine::create()
{
  return unique_ptr<WasmEngine>{new WavmEngine};
}

namespace wavm_host_module {
  // first the ethereum interface(s), the top of the stack is used in host functions
  stack<WavmEthereumInterface*> interface;


  // the host module is called 'ethereum'
  DEFINE_INTRINSIC_MODULE(ethereum)


  // host functions follow
  DEFINE_INTRINSIC_FUNCTION(ethereum, "useGas", void, useGas, I64 amount)
  {
    interface.top()->eeiUseGas(amount);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getAddress", void, getAddress, U32 resultOffset)
  {
    interface.top()->eeiGetAddress(resultOffset);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "call", U32, call, I64 gas, U32 addressOffset, U32 valueOffset, U32 dataOffset, U32 dataLength)
  {
    return interface.top()->eeiCall(EthereumInterface::EEICallKind::Call, gas, addressOffset, valueOffset, dataOffset, dataLength);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "callDataCopy", void, callDataCopy, U32 resultOffset, U32 dataOffset, U32 length)
  {
    interface.top()->eeiCallDataCopy(resultOffset, dataOffset, length);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getCallDataSize", U32, getCallDataSize)
  {
    return interface.top()->eeiGetCallDataSize();
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getGasLeft", U64, getGasLeft)
  {
    return static_cast<U64>(interface.top()->eeiGetGasLeft());
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "storageStore", void, storageStore, U32 pathOffset, U32 valueOffset)
  {
    interface.top()->eeiStorageStore(pathOffset, valueOffset);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "storageLoad", void, storageLoad, U32 pathOffset, U32 valueOffset)
  {
    interface.top()->eeiStorageLoad(pathOffset, valueOffset);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "codeCopy", void, codeCopy, U32 resultOffset, U32 codeOffset, U32 length)
  {
    interface.top()->eeiCodeCopy(resultOffset, codeOffset, length);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getCodeSize", U32, getCodeSize)
  {
    return interface.top()->eeiGetCodeSize();
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "finish", void, finish, U32 dataOffset, U32 length)
  {
    interface.top()->eeiFinish(dataOffset, length);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "revert", void, revert, U32 dataOffset, U32 length)
  {
    interface.top()->eeiRevert(dataOffset, length);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getReturnDataSize", U32, getReturnDataSize)
  {
    return interface.top()->eeiGetReturnDataSize();
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "returnDataCopy", void, returnDataCopy, U32 resultOffset, U32 dataOffset, U32 length)
  {
    return interface.top()->eeiReturnDataCopy(resultOffset, dataOffset, length);
  }


  // this is needed for resolving names of imported host functions
  struct HeraWavmResolver : Runtime::Resolver {
    Runtime::Compartment* compartment;
    HashMap<string, Runtime::ModuleInstance*> moduleNameToInstanceMap;

    HeraWavmResolver(Runtime::Compartment* inCompartment) : compartment(inCompartment) {}

    bool resolve(const string& moduleName,
      const string& exportName,
      IR::ObjectType type,
      Runtime::Object*& outObject) override
    {
      outObject = nullptr;
      auto namedInstance = moduleNameToInstanceMap.get(moduleName);
      if (namedInstance)
          outObject = Runtime::getInstanceExport(*namedInstance, exportName);
      return outObject != nullptr;
    }
  };
} // namespace wavm_host_module

ExecutionResult WavmEngine::internalExecute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  HERA_DEBUG << "Executing with wavm...\n";

  // set up a new ethereum interface just for this contract invocation
  ExecutionResult result;
  WavmEthereumInterface interface{context, state_code, msg, result, meterInterfaceGas};
  wavm_host_module::interface.push(&interface);

  // first parse module
  IR::Module irmodule;
  try {
    // NOTE: this expects U8, which is a typedef over uint8_t
    Serialization::MemoryInputStream input(code.data(), code.size());
    WASM::serialize(input, irmodule);
  } catch (Serialization::FatalSerializationException const& e) {
    ensureCondition(false, ContractValidationFailure, "Failed to deserialise contract: " + e.message);
  } catch (IR::ValidationException const& e) {
    ensureCondition(false, ContractValidationFailure, "Failed to validate contract: " + e.message);
  } catch (std::bad_alloc const&) {
    // Catching this here because apparently wavm doesn't necessarily checks bounds before allocation
    ensureCondition(false, ContractValidationFailure, "Bug in wavm: didn't check bounds before allocation");
  }

  // compile the module (not sure what this does)
  Runtime::Module* module = Runtime::compileModule(irmodule);

  // set up the VM
  // compartment is like the Wasm store, represents the VM, has lists of globals, memories, tables, and also has wavm's runtime stuff
  Runtime::GCPointer<Runtime::Compartment> compartment = Runtime::createCompartment();
  // context stores the compartment and some other stuff
  Runtime::GCPointer<Runtime::Context> wavm_context = Runtime::createContext(compartment);

  // set up the host Module
  // Note: in ewasm, we create a new VM for each call to a module, so we must instantiate a new host module for each of these VMs, this is inefficient, but OK for prototyping.
  HashMap<string, Runtime::Object*> extraEthereumExports; //empty for current ewasm stuff
  // instantiate host Module
  Runtime::GCPointer<Runtime::ModuleInstance> ethereumHostModule = Intrinsics::instantiateModule(compartment, wavm_host_module::INTRINSIC_MODULE_REF(ethereum), "ethereum", extraEthereumExports);
  heraAssert(ethereumHostModule, "Failed to create host module.");
  // prepare contract module to resolve links against host module
  wavm_host_module::HeraWavmResolver resolver(compartment);
  resolver.moduleNameToInstanceMap.set("ethereum", ethereumHostModule);
  Runtime::LinkResult linkResult = Runtime::linkModule(irmodule, resolver);
  heraAssert(linkResult.success, "Couldn't link contract against host module.");

  // instantiate contract module
  Runtime::GCPointer<Runtime::ModuleInstance> moduleInstance = Runtime::instantiateModule(compartment, module, move(linkResult.resolvedImports), "<ewasmcontract>");
  heraAssert(moduleInstance, "Couldn't instantiate contact module.");

  // get memory for easy access in host functions
  wavm_host_module::interface.top()->setWasmMemory(asMemory(Runtime::getInstanceExport(moduleInstance, "memory")));

  // invoke the main function
  Runtime::GCPointer<Runtime::FunctionInstance> mainFunction = asFunctionNullable(Runtime::getInstanceExport(moduleInstance, "main"));
  ensureCondition(mainFunction, ContractValidationFailure, "\"main\" not found");

  // this is how WAVM's try/catch for exceptions
  Runtime::catchRuntimeExceptions(
    [&] {
      try {
        vector<IR::Value> invokeArgs;
        Runtime::invokeFunctionChecked(wavm_context, mainFunction, invokeArgs);
      } catch (EndExecution const&) {
        // This exception is ignored here because we consider it to be a success.
        // It is only a clutch for POSIX style exit()
      }
    },
    [&](Runtime::Exception&& exception) {
      // FIXME: decide if each of the exception fit into VMTrap/InternalError
      ensureCondition(false, VMTrap, Runtime::describeException(exception));
    }
  );

  // clean up
  wavm_host_module::interface.pop();

  return result;
}

} // namespace hera
