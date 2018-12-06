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

#include "src/binary-reader-interp.h"
#include "src/binary-reader.h"
#include "src/cast.h"
#include "src/error-handler.h"
#include "src/feature.h"
#include "src/interp.h"
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

class WabtEthereumInterface : EthereumInterface, public interp::HostImportDelegate {
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

protected:
  Result ImportFunc(
    interp::FuncImport* import,
    interp::Func* func,
    interp::FuncSignature* func_sig,
    const ErrorCallback& callback
  ) override;

  Result ImportMemory(
    interp::MemoryImport* import,
    interp::Memory* mem,
    const ErrorCallback& callback
  ) override;

  Result ImportGlobal(
    interp::GlobalImport* import,
    interp::Global* global,
    const ErrorCallback& callback
  ) override;

  Result ImportTable(
    interp::TableImport* import,
    interp::Table* table,
    const ErrorCallback& callback
  ) override;

  static interp::Result wabtUseGas(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtGetGasLeft(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtStorageStore(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtStorageLoad(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtFinish(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtRevert(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtGetCallDataSize(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtCallDataCopy(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

  static interp::Result wabtGetCallValue(
    const interp::HostFunc* func,
    const interp::FuncSignature* sig,
    Index num_args,
    interp::TypedValue* args,
    Index num_results,
    interp::TypedValue* out_results,
    void* user_data
  );

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

Result WabtEthereumInterface::ImportFunc(
  interp::FuncImport* import,
  interp::Func* func,
  interp::FuncSignature* func_sig,
  const ErrorCallback& callback
) {
  (void)import;
  (void)func;
  (void)callback;
  HERA_DEBUG << "Importing " << import->field_name << "\n";
  interp::HostFunc *hostFunc = reinterpret_cast<interp::HostFunc*>(func);
  if (import->field_name == "useGas") {
    if (func_sig->param_types.size() != 1 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtUseGas;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "getGasLeft") {
    if (func_sig->param_types.size() != 0 || func_sig->result_types.size() != 1)
      return Result::Error;
    hostFunc->callback = wabtGetGasLeft;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "storageStore") {
    if (func_sig->param_types.size() != 2 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtStorageStore;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "storageLoad") {
    if (func_sig->param_types.size() != 2 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtStorageLoad;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "finish") {
    if (func_sig->param_types.size() != 2 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtFinish;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "revert") {
    if (func_sig->param_types.size() != 2 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtRevert;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "getCallDataSize") {
    if (func_sig->param_types.size() != 0 || func_sig->result_types.size() != 1)
      return Result::Error;
    hostFunc->callback = wabtGetCallDataSize;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "callDataCopy") {
    if (func_sig->param_types.size() != 3 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtCallDataCopy;
    hostFunc->user_data = this;
    return Result::Ok;
  } else if (import->field_name == "getCallValue") {
    if (func_sig->param_types.size() != 1 || func_sig->result_types.size() != 0)
      return Result::Error;
    hostFunc->callback = wabtGetCallValue;
    hostFunc->user_data = this;
    return Result::Ok;
  }
  return Result::Error;
}

Result WabtEthereumInterface::ImportMemory(
  interp::MemoryImport* import,
  interp::Memory* mem,
  const ErrorCallback& callback
) {
  (void)import;
  (void)mem;
  (void)callback;
  return Result::Error;
}

Result WabtEthereumInterface::ImportGlobal(
  interp::GlobalImport* import,
  interp::Global* global,
  const ErrorCallback& callback
) {
  (void)import;
  (void)global;
  (void)callback;
  return Result::Error;
}

Result WabtEthereumInterface::ImportTable(
  interp::TableImport* import,
  interp::Table* table,
  const ErrorCallback& callback
) {
  (void)import;
  (void)table;
  (void)callback;
  return Result::Error;
}

interp::Result WabtEthereumInterface::wabtUseGas(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  int64_t gas = static_cast<int64_t>(args[0].value.i64);

  // FIXME: handle host trap here
  interface->eeiUseGas(gas);

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtGetGasLeft(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)num_results;
  (void)args;
  (void)num_args;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  out_results[0].type = sig->result_types[0];
  out_results[0].value.i64 = static_cast<uint64_t>(interface->eeiGetGasLeft());

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtStorageStore(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  uint32_t pathOffset = args[0].value.i32;
  uint32_t valueOffset = args[1].value.i32;
  
  interface->eeiStorageStore(pathOffset, valueOffset);

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtStorageLoad(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  uint32_t pathOffset = args[0].value.i32;
  uint32_t valueOffset = args[1].value.i32;
  
  interface->eeiStorageLoad(pathOffset, valueOffset);

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtFinish(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  uint32_t offset = args[0].value.i32;
  uint32_t length = args[1].value.i32;

  // FIXME: handle host trap here
  interface->eeiFinish(offset, length);

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtRevert(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  uint32_t offset = args[0].value.i32;
  uint32_t length = args[1].value.i32;

  // FIXME: handle host trap here
  interface->eeiRevert(offset, length);

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtGetCallDataSize(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)num_results;
  (void)args;
  (void)num_args;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  out_results[0].type = sig->result_types[0];
  out_results[0].value.i32 = interface->eeiGetCallDataSize();

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtCallDataCopy(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  uint32_t resultOffset = args[0].value.i32;
  uint32_t dataOffset = args[1].value.i32;
  uint32_t length = args[2].value.i32;

  interface->eeiCallDataCopy(resultOffset, dataOffset, length);

  return interp::Result::Ok;
}

interp::Result WabtEthereumInterface::wabtGetCallValue(
  const interp::HostFunc* func,
  const interp::FuncSignature* sig,
  Index num_args,
  interp::TypedValue* args,
  Index num_results,
  interp::TypedValue* out_results,
  void* user_data
) {
  (void)func;
  (void)sig;
  (void)num_args;
  (void)num_results;
  (void)out_results;

  WabtEthereumInterface *interface = reinterpret_cast<WabtEthereumInterface*>(user_data);

  uint32_t resultOffset = args[0].value.i32;
  
  interface->eeiGetCallValue(resultOffset);

  return interp::Result::Ok;
}

ExecutionResult WabtEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  HERA_DEBUG << "Executing with wabt...\n";

  // This is the wasm state
  interp::Environment env;

  // Lets instantiate our state
  ExecutionResult result;

  // FIXME: shouldn't have this loose pointer here, but needed for setWasmMemory
  WabtEthereumInterface* interface = new WabtEthereumInterface{context, state_code, msg, result, meterInterfaceGas};

  // Lets add our host module
  // The lifecycle of this pointer is handled by `env`.
  interp::HostModule* hostModule = env.AppendHostModule("ethereum");
  heraAssert(hostModule, "Failed to create host module.");
  hostModule->import_delegate = unique_ptr<WabtEthereumInterface>(interface);

  ReadBinaryOptions options(
    Features{},
    nullptr, // debugging stream for loading
    false, // ReadDebugNames
    true, // StopOnFirstError
    true // FailOnCustomSectionError
  );

  ErrorHandlerFile error_handler(Location::Type::Binary);
  interp::DefinedModule* module = nullptr;
  ReadBinaryInterp(
    &env,
    code.data(),
    code.size(),
    &options,
    &error_handler,
    &module
  );
  ensureCondition(module, ContractValidationFailure, "Module failed to load.");
  ensureCondition(env.GetMemoryCount() == 1, ContractValidationFailure, "Multiple memory sections exported.");

  ensureCondition(module->start_func_index == kInvalidIndex, ContractValidationFailure, "Contract contains start function.");

  interp::Export* mainFunction = module->GetExport("main");
  ensureCondition(mainFunction, ContractValidationFailure, "\"main\" not found");
  ensureCondition(mainFunction->kind == ExternalKind::Func, ContractValidationFailure,  "\"main\" is not a function");

  // No tracing, no threads
  interp::Executor executor(&env, nullptr, interp::Thread::Options{});

  // FIXME: really bad design
  interface->setWasmMemory(env.GetMemory(0));

  // Execute main
  try {
    interp::ExecResult wabtResult = executor.RunExport(mainFunction, interp::TypedValues{});
  } catch (EndExecution const&) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
  }

  return result;
}

}
