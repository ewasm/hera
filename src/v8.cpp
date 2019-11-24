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

#include <iostream>

#include "v8.h"
#include "debugging.h"
#include "eei.h"
#include "exceptions.h"

using namespace std;

namespace hera {

std::unique_ptr<WasmEngine> hera::V8Engine::create() {
  return nullptr;
}

ExecutionResult hera::V8Engine::execute(evmc_context *context,
                                        bytes_view code,
                                        bytes_view state_code,
                                        const evmc_message &msg,
                                        bool meterInterfaceGas) {
  (void) context;
  (void) code;
  (void) state_code;
  (void) msg;
  (void) meterInterfaceGas;

  ExecutionResult result{};
  return result;
}

void hera::V8Engine::verifyContract(hera::bytes_view code) {
  (void) code;
}

} // namespace hera
