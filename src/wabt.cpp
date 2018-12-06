/*
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

#include <vector>
#include <iostream>

#include "src/interp/binary-reader-interp.h"
#include "src/binary-reader.h"
#include "src/cast.h"
#include "src/feature.h"
#include "src/interp/interp.h"
#include "src/literal.h"
#include "src/option-parser.h"
#include "src/resolve-names.h"
#include "src/stream.h"
#include "src/validator.h"
#include "src/wast-lexer.h"
#include "src/wast-parser.h"

#include "wabt.h"
#include "debugging.h"
#include "eei.h"
#include "exceptions.h"

using namespace std;
using namespace wabt;

namespace hera {

class WabtEthereumInterface : public EthereumInterface {
public:
  explicit WabtEthereumInterface(
    evmc_context* _context,
    vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    EthereumInterface(_context, _code, _msg, _result, _meterGas)
  {}

  // TODO: improve this design...
  void setWasmMemory(interp::Memory* _wasmMemory) {
    m_wasmMemory = _wasmMemory;
  }

private:
  // These assume that m_wasmMemory was set prior to execution.
  size_t memorySize() const override { return m_wasmMemory->data.size(); }
  void memorySet(size_t offset, uint8_t value) override { m_wasmMemory->data[offset] = static_cast<char>(value); }
  uint8_t memoryGet(size_t offset) override { return static_cast<uint8_t>(m_wasmMemory->data[offset]); }

  interp::Memory* m_wasmMemory;
};

unique_ptr<WasmEngine> WabtEngine::create()
{
  return unique_ptr<WasmEngine>{new WabtEngine};
}

ExecutionResult WabtEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  HERA_DEBUG << "Executing with wabt...\n";

  // Set up the wabt Environment, which includes the Wasm store
  // and the list of modules used for importing/exporting between modules
  interp::Environment env;

  // Set up interface to eei host functions
  ExecutionResult result;
  // FIXME: shouldn't have this loose pointer here, but needed for setWasmMemory
  WabtEthereumInterface* interface = new WabtEthereumInterface{context, state_code, msg, result, meterInterfaceGas};

  // Create host module
  // The lifecycle of this pointer is handled by `env`.
  interp::HostModule* hostModule = env.AppendHostModule("ethereum");
  heraAssert(hostModule, "Failed to create host module.");

  hostModule->AppendFuncExport(
    "useGas",
    {{Type::I64}, {}},
    [&interface](
      const interp::HostFunc*,
      const interp::FuncSignature*,
      const interp::TypedValues& args,
      interp::TypedValues&
    ) {
      int64_t gas = static_cast<int64_t>(args[0].value.i64);
      // FIXME: handle host trap here
      interface->eeiUseGas(gas);
      return interp::Result::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "getGasLeft",
    {{}, {Type::I64}},
    [&interface](
      const interp::HostFunc*,
      const interp::FuncSignature*,
      const interp::TypedValues&,
      interp::TypedValues& results
    ) {
      results[0].set_i64(static_cast<uint64_t>(interface->eeiGetGasLeft()));
      return interp::Result::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "storageStore",
    {{Type::I32, Type::I32}, {}},
    [&interface](
      const interp::HostFunc*,
      const interp::FuncSignature*,
      const interp::TypedValues& args,
      interp::TypedValues&
    ) {
      interface->eeiStorageStore(args[0].get_i32(), args[1].get_i32());
      return interp::Result::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "storageLoad",
    {{Type::I32, Type::I32}, {}},
    [&interface](
      const interp::HostFunc*,
      const interp::FuncSignature*,
      const interp::TypedValues& args,
      interp::TypedValues&
    ) {
      interface->eeiStorageLoad(args[0].get_i32(), args[1].get_i32());
      return interp::Result::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "finish",
    {{Type::I32, Type::I32}, {}},
    [&interface](
      const interp::HostFunc*,
      const interp::FuncSignature*,
      const interp::TypedValues& args,
      interp::TypedValues&
    ) {
      // FIXME: handle host trap here
      interface->eeiFinish(args[0].get_i32(), args[1].get_i32());
      return interp::Result::Ok;
    }
  );

  hostModule->AppendFuncExport(
    "revert",
    {{Type::I32, Type::I32}, {}},
    [&interface](
      const interp::HostFunc*,
      const interp::FuncSignature*,
      const interp::TypedValues& args,
      interp::TypedValues&
    ) {
      // FIXME: handle host trap here
      interface->eeiRevert(args[0].get_i32(), args[1].get_i32());
      return interp::Result::Ok;
    }
  );

  // Parse module
  ReadBinaryOptions options(
    Features{},
    nullptr, // debugging stream for loading
    false, // ReadDebugNames
    true, // StopOnFirstError
    true // FailOnCustomSectionError
  );

  Errors errors;
  interp::DefinedModule* module = nullptr;
  Result loadResult = ReadBinaryInterp(
    &env,
    code.data(),
    code.size(),
    options,
    &errors,
    &module
  );

  ensureCondition(Succeeded(loadResult) && module, ContractValidationFailure, "Module failed to load.");
  ensureCondition(env.GetMemoryCount() == 1, ContractValidationFailure, "Multiple memory sections exported.");
  ensureCondition(module->start_func_index == kInvalidIndex, ContractValidationFailure, "Contract contains start function.");

  // Prepare to execute
  interp::Export* mainFunction = module->GetExport("main");
  ensureCondition(mainFunction, ContractValidationFailure, "\"main\" not found");
  ensureCondition(mainFunction->kind == ExternalKind::Func, ContractValidationFailure,  "\"main\" is not a function");
  interp::Executor executor(
    &env,
    nullptr, // null for no tracing
    interp::Thread::Options{} // empty for no threads
  );

  // FIXME: really bad design
  interface->setWasmMemory(env.GetMemory(0));

  // Execute main
  try {
    interp::ExecResult wabtResult = executor.RunExport(mainFunction, interp::TypedValues{}); // second arg is empty since no args
  } catch (EndExecution const&) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
  }

  return result;
}

}
