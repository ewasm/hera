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

#include <iostream>
#include <memory>
#include <stack>

#include "wavm.h"

#define DLL_IMPORT // Needed by wavm on some platforms
#include "Inline/Serialization.h"
#include "IR/Module.h"
#include "IR/Validate.h"
#include "Runtime/Intrinsics.h"
#include "Runtime/Linker.h"
#include "Runtime/Runtime.h"
#include "WASM/WASM.h"

#include "debugging.h"
#include "eei.h"
#include "exceptions.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

using namespace std;

namespace hera {

class WavmEthereumInterface : public EthereumInterface {
public:
  explicit WavmEthereumInterface(
    evmc_context* _context,
    vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    EthereumInterface(_context, _code, _msg, _result, _meterGas)
  {}

  void setWasmMemory(Runtime::MemoryInstance* _wasmMemory) {
    m_wasmMemory = _wasmMemory;
  }

private:
  // These assume that m_wasmMemory was set prior to execution.
  size_t memorySize() const override { return Runtime::getMemoryNumPages(m_wasmMemory) * 65536; }
  void memorySet(size_t offset, uint8_t value) override { (Runtime::memoryArrayPtr<U8>(m_wasmMemory, offset, 1))[0] = value; }
  uint8_t memoryGet(size_t offset) override { return (Runtime::memoryArrayPtr<U8>(m_wasmMemory, offset, 1))[0]; }

  Runtime::MemoryInstance* m_wasmMemory;
};

unique_ptr<WasmEngine> WavmEngine::create()
{
  return unique_ptr<WasmEngine>{new WavmEngine};
}

namespace wavm_host_module {
  // first the ethereum interface(s), the top of the stack is used in host functions
  stack<WavmEthereumInterface*> interface;

  // set up for re-throwing exceptions, this is not clean, but Hera and WAVM are incompatible without some awkwardness
  std::exception_ptr eei_exception_ptr = nullptr;
  bool VMTrapFlag = false;

  // the host module is called 'ethereum'
  DEFINE_INTRINSIC_MODULE(ethereum)


  // host functions follow
  DEFINE_INTRINSIC_FUNCTION(ethereum, "useGas", void, useGas, I64 amount)
  {
    try {
      interface.top()->eeiUseGas(amount);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getAddress", void, getAddress, U32 resultOffset)
  {
    try {
      interface.top()->eeiGetAddress(resultOffset);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "call", U32, call, I64 gas, U32 addressOffset, U32 valueOffset, U32 dataOffset, U32 dataLength)
  {
    U32 ret;
    try {
      ret = interface.top()->eeiCall(EthereumInterface::EEICallKind::Call, gas, addressOffset, valueOffset, dataOffset, dataLength);
      // Now that we have returned from the contract call, must clear out any exception occurring in the contract called. Note that the following code is skipped if an exception occurred when trying to call.
      eei_exception_ptr = nullptr;
      VMTrapFlag = false;
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
    return ret;
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "callDataCopy", void, callDataCopy, U32 resultOffset, U32 dataOffset, U32 length)
  {
    try {
      interface.top()->eeiCallDataCopy(resultOffset, dataOffset, length);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getCallDataSize", U32, getCallDataSize)
  {
    U32 ret;
    try {
      ret = interface.top()->eeiGetCallDataSize();
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
    return ret;
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getCaller", void, getCaller, U32 resultOffset)
  {
    interface.top()->eeiGetCaller(resultOffset);
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getGasLeft", U64, getGasLeft)
  {
    U64 ret;
    try {
      ret = static_cast<U64>(interface.top()->eeiGetGasLeft());
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
    return ret;
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "storageStore", void, storageStore, U32 pathOffset, U32 valueOffset)
  {
    try {
      interface.top()->eeiStorageStore(pathOffset, valueOffset);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "storageLoad", void, storageLoad, U32 pathOffset, U32 valueOffset)
  {
    try {
      interface.top()->eeiStorageLoad(pathOffset, valueOffset);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "codeCopy", void, codeCopy, U32 resultOffset, U32 codeOffset, U32 length)
  {
    try {
      interface.top()->eeiCodeCopy(resultOffset, codeOffset, length);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getCodeSize", U32, getCodeSize)
  {
    U32 ret;
    try {
      ret =interface.top()->eeiGetCodeSize();
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
    return ret;
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "finish", void, finish, U32 dataOffset, U32 length)
  {
    try {
      interface.top()->eeiFinish(dataOffset, length);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "revert", void, revert, U32 dataOffset, U32 length)
  {
    try {
      interface.top()->eeiRevert(dataOffset, length);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "getReturnDataSize", U32, getReturnDataSize)
  {
    U32 ret;
    try {
      ret = interface.top()->eeiGetReturnDataSize();
    } catch (HeraException const& e) {
      HERA_DEBUG<<"caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
    return ret;
  }


  DEFINE_INTRINSIC_FUNCTION(ethereum, "returnDataCopy", void, returnDataCopy, U32 resultOffset, U32 dataOffset, U32 length)
  {
    try {
      interface.top()->eeiReturnDataCopy(resultOffset, dataOffset, length);
    } catch (HeraException const& e) {
      HERA_DEBUG << "caught Hera's Exception\n";
      // save exception so that we can rethrow it once WAVM returns from the invocation
      eei_exception_ptr = std::current_exception();
      // clear pointer to memory and trigger WAVM halt so that WAVM cleans up and exits
      interface.top()->setWasmMemory(nullptr);
      throwException(Runtime::Exception::calledUnimplementedIntrinsicType); //or maybe reachedUnreachableType
    }
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
      if (outObject && !isA(outObject, type))
        outObject = nullptr;
      HERA_DEBUG << "Resolving " << moduleName << " : " << exportName << " -> " << (outObject != nullptr) << "\n";
      return outObject != nullptr;
    }
  };
} // namespace wavm_host_module


ExecutionResult WavmEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {

  // clear stuff from previous run
  wavm_host_module::eei_exception_ptr = nullptr;
  wavm_host_module::VMTrapFlag = false;

  // hope and prayer
  Runtime::collectGarbage();

  // execute the contract
  ExecutionResult result;
  try {
    result = internalExecute(context, code, state_code, msg, meterInterfaceGas);

    // throw VMTrap exception if there was an exception thrown by WAVM and not by us
    if(wavm_host_module::VMTrapFlag)
      ensureCondition(false, VMTrap, "hera exception from WAVM VM Trap")

    // re-throw exception if there was one
    if (wavm_host_module::eei_exception_ptr)
      std::rethrow_exception (wavm_host_module::eei_exception_ptr);
  } catch (EndExecution const&) {
      HERA_DEBUG << "caught Hera's EndExecution\n";
      // This exception is ignored here because we consider it to be a success.
      // It is only a clutch for POSIX style exit()
  }

  // clean up this run, this is done here after leaving the scope of internalExecute()
  Runtime::collectGarbage();

  return result;
}

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
  IR::Module moduleIR;
  try {
    // NOTE: this expects U8, which is a typedef over uint8_t
    Serialization::MemoryInputStream input(code.data(), code.size());
    WASM::serialize(input, moduleIR);
  } catch (Serialization::FatalSerializationException const& e) {
    ensureCondition(false, ContractValidationFailure, "Failed to deserialise contract: " + e.message);
  } catch (IR::ValidationException const& e) {
    ensureCondition(false, ContractValidationFailure, "Failed to validate contract: " + e.message);
  } catch (std::bad_alloc const&) {
    // Catching this here because apparently wavm doesn't necessarily checks bounds before allocation
    ensureCondition(false, ContractValidationFailure, "Bug in wavm: didn't check bounds before allocation");
  }

  // next set up the VM
  // Note: in ewasm, we create a new VM for each call to a module, so we must instantiate a new host module for each of these VMs, this is inefficient, but OK for prototyping.

  // compartment is like the Wasm store, represents the VM, has lists of globals, memories, tables, and also has wavm's runtime stuff
  Runtime::GCPointer<Runtime::Compartment> compartment = Runtime::createCompartment();
  // context stores the compartment and some other stuff
  Runtime::GCPointer<Runtime::Context> wavm_context = Runtime::createContext(compartment);

  // instantiate host Module
  HashMap<string, Runtime::Object*> extraEthereumExports; //empty for current ewasm stuff
  Runtime::GCPointer<Runtime::ModuleInstance> ethereumHostModule = Intrinsics::instantiateModule(compartment, wavm_host_module::INTRINSIC_MODULE_REF(ethereum), "ethereum", extraEthereumExports);
  heraAssert(ethereumHostModule, "Failed to create host module.");

  // prepare contract module to resolve links against host module
  wavm_host_module::HeraWavmResolver resolver(compartment);
  resolver.moduleNameToInstanceMap.set("ethereum", ethereumHostModule);
  Runtime::LinkResult linkResult = Runtime::linkModule(moduleIR, resolver);
  heraAssert(linkResult.success, "Couldn't link contract against host module.");

  // compile the module from IR to LLVM bitcode
  Runtime::GCPointer<Runtime::Module> module = Runtime::compileModule(moduleIR);

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
        vector<IR::Value> invokeArgs;
        Runtime::invokeFunctionChecked(wavm_context, mainFunction, invokeArgs);
        // wavm may have cleaned up the memory, so make sure we can't access it
        wavm_host_module::interface.top()->setWasmMemory(nullptr);
    },
    [&](Runtime::Exception&& exception) {
      HERA_DEBUG << "caught WAVM's Runtime::Exception\n";
      // wavm may have cleaned up the memory, so make sure we can't access it
      wavm_host_module::interface.top()->setWasmMemory(nullptr);
      // if WAVM threw an exception and we did not, then make a note so we can throw a corresponding hera exception
      if (wavm_host_module::eei_exception_ptr == nullptr)
        wavm_host_module::VMTrapFlag = true;
    }
  );

  // clean up
  wavm_host_module::interface.top()->setWasmMemory(nullptr);
  wavm_host_module::interface.pop();
  compartment = nullptr;
  wavm_context = nullptr;
  ethereumHostModule = nullptr;
  moduleInstance = nullptr;
  mainFunction = nullptr;
  Runtime::collectGarbage();

  return result;
}

} // namespace hera
