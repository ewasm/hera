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

#pragma once

#include <iostream>
#include <memory>
#include <stack>

#define DLL_IMPORT // Needed by wavm on some platforms
#include "Inline/Serialization.h"
#include "IR/Module.h"
#include "IR/Validate.h"
#include "Runtime/Intrinsics.h"
#include "Runtime/Linker.h"
#include "Runtime/Runtime.h"
#include "WASM/WASM.h"

#include "eei.h"

namespace hera {

class WavmEthereumInterface : public EthereumInterface {

public:
  explicit WavmEthereumInterface(
    evmc_context* _context,
    std::vector<uint8_t> const& _code,
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

class WavmEngine : public WasmEngine {
public:
  /// Factory method to create the WABT Wasm Engine.
  static std::unique_ptr<WasmEngine> create();

  ExecutionResult execute(
    evmc_context* context,
    std::vector<uint8_t> const& code,
    std::vector<uint8_t> const& state_code,
    evmc_message const& msg,
    bool meterInterfaceGas
  ) override {
    ExecutionResult result = internalExecute(context, code, state_code, msg, meterInterfaceGas);
    // And clean up mess left by this run.
    Runtime::collectGarbage();
    return result;
  }

private:
  ExecutionResult internalExecute(
    evmc_context* context,
    std::vector<uint8_t> const& code,
    std::vector<uint8_t> const& state_code,
    evmc_message const& msg,
    bool meterInterfaceGas
  );
};

} // namespace hera
