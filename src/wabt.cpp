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

std::unique_ptr<WasmEngine> WabtEngine::create()
{
  return std::unique_ptr<WasmEngine>{new WabtEngine};
}

wabt::Result WabtEthereumInterface::ImportFunc(
  wabt::interp::FuncImport* import,
  wabt::interp::Func* func,
  wabt::interp::FuncSignature* func_sig,
  const ErrorCallback& callback
) {
  (void)import;
  (void)func;
  (void)callback;
  HERA_DEBUG << "Importing " << import->field_name << "\n";
  wabt::interp::HostFunc *hostFunc = reinterpret_cast<wabt::interp::HostFunc*>(func);
  if (import->field_name == "useGas") {
    if (func_sig->param_types.size() != 1 || func_sig->result_types.size() != 0)
      return wabt::Result::Error;
    hostFunc->callback = wabtUseGas;
    hostFunc->user_data = this;
    return wabt::Result::Ok;
  } else if (import->field_name == "finish") {
    if (func_sig->param_types.size() != 2 || func_sig->result_types.size() != 0)
      return wabt::Result::Error;
    hostFunc->callback = wabtFinish;
    hostFunc->user_data = this;
    return wabt::Result::Ok;
  } else if (import->field_name == "revert") {
    if (func_sig->param_types.size() != 2 || func_sig->result_types.size() != 0)
      return wabt::Result::Error;
    hostFunc->callback = wabtRevert;
    hostFunc->user_data = this;
    return wabt::Result::Ok;
  } else if (import->field_name == "getCallDataSize") {
    if (func_sig->param_types.size() != 0 || func_sig->result_types.size() != 1)
      return wabt::Result::Error;
    hostFunc->callback = wabtGetCallDataSize;
    hostFunc->user_data = this;
    return wabt::Result::Ok;
  } else if (import->field_name == "callDataCopy") {
    if (func_sig->param_types.size() != 3 || func_sig->result_types.size() != 0)
      return wabt::Result::Error;
    hostFunc->callback = wabtCallDataCopy;
    hostFunc->user_data = this;
    return wabt::Result::Ok;
  }
  return wabt::Result::Error;
}

wabt::Result WabtEthereumInterface::ImportMemory(
  wabt::interp::MemoryImport* import,
  wabt::interp::Memory* mem,
  const ErrorCallback& callback
) {
  (void)import;
  (void)mem;
  (void)callback;
  return wabt::Result::Error;
}

wabt::Result WabtEthereumInterface::ImportGlobal(
  wabt::interp::GlobalImport* import,
  wabt::interp::Global* global,
  const ErrorCallback& callback
) {
  (void)import;
  (void)global;
  (void)callback;
  return wabt::Result::Error;
}

wabt::Result WabtEthereumInterface::ImportTable(
  wabt::interp::TableImport* import,
  wabt::interp::Table* table,
  const ErrorCallback& callback
) {
  (void)import;
  (void)table;
  (void)callback;
  return wabt::Result::Error;
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

ExecutionResult WabtEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  HERA_DEBUG << "Executing with wabt...\n";

  // This is the wasm state
  wabt::interp::Environment env;

  // Lets instantiate our state
  ExecutionResult result;

  // FIXME: shouldn't have this loose pointer here, but needed for setWasmMemory
  WabtEthereumInterface* interface = new WabtEthereumInterface{context, state_code, msg, result, meterInterfaceGas};

  // Lets add our host module
  // The lifecycle of this pointer is handled by `env`.
  wabt::interp::HostModule* hostModule = env.AppendHostModule("ethereum");
  heraAssert(hostModule, "Failed to create host module.");
  hostModule->import_delegate = std::unique_ptr<WabtEthereumInterface>(interface);

  wabt::ReadBinaryOptions options(
    wabt::Features{},
    nullptr, // debugging stream for loading
    false, // ReadDebugNames
    true, // StopOnFirstError
    true // FailOnCustomSectionError
  );

  wabt::ErrorHandlerFile error_handler(wabt::Location::Type::Binary);
  wabt::interp::DefinedModule* module = nullptr;
  wabt::ReadBinaryInterp(
    &env,
    code.data(),
    code.size(),
    &options,
    &error_handler,
    &module
  );
  ensureCondition(module, ContractValidationFailure, "Module failed to load.");
  ensureCondition(env.GetMemoryCount() == 1, ContractValidationFailure, "Multiple memory sections exported.");

  wabt::interp::Export* mainFunction = module->GetExport("main");
  ensureCondition(mainFunction, ContractValidationFailure, "\"main\" not found");
  ensureCondition(mainFunction->kind == wabt::ExternalKind::Func, ContractValidationFailure,  "\"main\" is not a function");

  // No tracing, no threads
  wabt::interp::Executor executor(&env, nullptr, wabt::interp::Thread::Options{});

  // FIXME: really bad design
  interface->setWasmMemory(env.GetMemory(0));

  // Execute main
  try {
    wabt::interp::ExecResult wabtResult = executor.RunExport(mainFunction, wabt::interp::TypedValues{});
  } catch (EndExecution const&) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
  }

  return result;
}

}
