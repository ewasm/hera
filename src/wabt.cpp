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

#include <src/binary-writer.h>
#include <src/common.h>
#include <src/error-handler.h>
#include <src/feature.h>
#include <src/filenames.h>
#include <src/ir.h>
#include <src/option-parser.h>
#include <src/resolve-names.h>
#include <src/stream.h>
#include <src/validator.h>
#include <src/wast-parser.h>

#include "wabt.h"
#include "eei.h"
#include "exceptions.h"

using namespace std;

namespace hera {

ExecutionResult WabtEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  (void)context;
  (void)code;
  (void)state_code;
  (void)msg;
  (void)meterInterfaceGas;

  std::unique_ptr<wabt::WastLexer> lexer = wabt::WastLexer::CreateFileLexer({});

  wabt::Features s_features;

  wabt::ErrorHandlerFile error_handler(wabt::Location::Type::Text);
  std::unique_ptr<wabt::Module> module;
  wabt::WastParseOptions parse_wast_options(s_features);
  wabt::Result result =
      wabt::ParseWatModule(lexer.get(), &module, &error_handler, &parse_wast_options);

  if (wabt::Succeeded(result)) {
    result = wabt::ResolveNamesModule(lexer.get(), module.get(), &error_handler);

    if (wabt::Succeeded(result)) {
      wabt::ValidateOptions options(s_features);
      result =
          wabt::ValidateModule(lexer.get(), module.get(), &error_handler, &options);
    }

    if (wabt::Succeeded(result)) {
      wabt::FileStream s_log_stream{wabt::string_view{}, nullptr};
      wabt::MemoryStream stream(&s_log_stream);
      result =
          wabt::WriteBinaryModule(&stream, module.get(), nullptr);

      if (wabt::Succeeded(result)) {
        // Success!
      }
    }
  }

  return ExecutionResult{};
}

}
