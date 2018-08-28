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

#include <wasm.h>
#include <wasm-binary.h>

#include "eei.h"

#include "shell-interface.h"

namespace hera {

class BinaryenEthereumInterface : public wasm::ShellExternalInterface, EthereumInterface {
public:
  explicit BinaryenEthereumInterface(
    evmc_context* _context,
    std::vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    ShellExternalInterface(),
    EthereumInterface(_context, _code, _msg, _result, _meterGas)
  { }

protected:
  wasm::Literal callImport(wasm::Import *import, wasm::LiteralList& arguments) override;
#if HERA_DEBUGGING
  wasm::Literal callDebugImport(wasm::Import *import, wasm::LiteralList& arguments);
#endif

  void importGlobals(std::map<wasm::Name, wasm::Literal>& globals, wasm::Module& wasm) override;

  void trap(const char* why) override {
    ensureCondition(false, VMTrap, why);
  }

private:
  size_t memorySize() const override { return memory.size(); }
  void memorySet(size_t offset, uint8_t value) override { memory.set<uint8_t>(offset, value); }
  uint8_t memoryGet(size_t offset) override { return memory.get<uint8_t>(offset); }
};

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

private:
  static void validate_contract(wasm::Module & module);
};

}
