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
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>
#include <string.h>
#include <fstream>
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

ExecutionResult WabtEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  (void)context;
  (void)state_code;
  (void)msg;
  (void)meterInterfaceGas;
  
  HERA_DEBUG << "Executing with wabt...\n";

  // This is the wasm state
  wabt::interp::Environment env;

  // Lets add our host module
  // TODO: append import delegate here

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

  wabt::interp::Export* mainFunction = module->GetExport("main");
  ensureCondition(mainFunction, ContractValidationFailure, "\"main\" not found");
  ensureCondition(mainFunction->kind == wabt::ExternalKind::Func, ContractValidationFailure,  "\"main\" is not a function");

  // No tracing, no threads
  wabt::interp::Executor executor(&env, nullptr, wabt::interp::Thread::Options{});
  
  // Execute main
  wabt::interp::ExecResult result = executor.RunExport(mainFunction, wabt::interp::TypedValues{});

  // FIXME populate output

  return ExecutionResult{};
}

}
