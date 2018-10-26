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

#pragma once

#include "eei.h"

namespace wasm {
class Module;
}

namespace hera {

class BinaryenEngine : public WasmEngine {
public:
  /// Factory method to create the Binaryen Wasm Engine.
  static std::unique_ptr<WasmEngine> create();

  ExecutionResult execute(
    evmc_context* context,
    std::vector<uint8_t> const& code,
    std::vector<uint8_t> const& state_code,
    evmc_message const& msg,
    bool meterInterfaceGas
  ) override;

  void verifyContract(std::vector<uint8_t> const& code) override;

private:
  void verifyContract(wasm::Module & module);

  /// Parses and loads a Wasm module.
  /// Don't ask, Module has no copy constructor, hence the reference.
  void loadModule(std::vector<uint8_t> const& code, wasm::Module & module);
};

}
