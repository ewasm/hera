/*
 * Copyright 2019 Alexander Arlt <alexander.arlt@arlt-labs.com>
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

namespace IR {
class Module;
}

namespace hera {

class V8Engine : public WasmEngine {
 public:
  /// Factory method to create the WAVM Wasm Engine.
  static std::unique_ptr<WasmEngine> create();

  ExecutionResult execute(
      evmc_context* context,
      bytes_view code,
      bytes_view state_code,
      evmc_message const& msg,
      bool meterInterfaceGas
  ) override;

  void verifyContract(bytes_view code) override;
};

} // namespace hera
