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

#include <src/interp.h>

#include "eei.h"

namespace hera {

class WabtEthereumInterface : EthereumInterface, public wabt::interp::HostImportDelegate {
public:
  explicit WabtEthereumInterface(
    evmc_context* _context,
    std::vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    EthereumInterface(_context, _code, _msg, _result, _meterGas)
  {}

  // TODO: improve this design...
  void setWasmMemory(wabt::interp::Memory* _wasmMemory) {
    m_wasmMemory = _wasmMemory;
  }

protected:
  wabt::Result ImportFunc(
    wabt::interp::FuncImport* import,
    wabt::interp::Func* func,
    wabt::interp::FuncSignature* func_sig,
    const ErrorCallback& callback
  ) override;

  wabt::Result ImportMemory(
    wabt::interp::MemoryImport* import,
    wabt::interp::Memory* mem,
    const ErrorCallback& callback
  ) override;

  wabt::Result ImportGlobal(
    wabt::interp::GlobalImport* import,
    wabt::interp::Global* global,
    const ErrorCallback& callback
  ) override;

  wabt::Result ImportTable(
    wabt::interp::TableImport* import,
    wabt::interp::Table* table,
    const ErrorCallback& callback
  ) override;

  static wabt::interp::Result wabtUseGas(
    const wabt::interp::HostFunc* func,
    const wabt::interp::FuncSignature* sig,
    wabt::Index num_args,
    wabt::interp::TypedValue* args,
    wabt::Index num_results,
    wabt::interp::TypedValue* out_results,
    void* user_data
  );

  static wabt::interp::Result wabtFinish(
    const wabt::interp::HostFunc* func,
    const wabt::interp::FuncSignature* sig,
    wabt::Index num_args,
    wabt::interp::TypedValue* args,
    wabt::Index num_results,
    wabt::interp::TypedValue* out_results,
    void* user_data
  );

  static wabt::interp::Result wabtRevert(
    const wabt::interp::HostFunc* func,
    const wabt::interp::FuncSignature* sig,
    wabt::Index num_args,
    wabt::interp::TypedValue* args,
    wabt::Index num_results,
    wabt::interp::TypedValue* out_results,
    void* user_data
  );

  static wabt::interp::Result wabtGetCallDataSize(
    const wabt::interp::HostFunc* func,
    const wabt::interp::FuncSignature* sig,
    wabt::Index num_args,
    wabt::interp::TypedValue* args,
    wabt::Index num_results,
    wabt::interp::TypedValue* out_results,
    void* user_data
  );

  static wabt::interp::Result wabtCallDataCopy(
    const wabt::interp::HostFunc* func,
    const wabt::interp::FuncSignature* sig,
    wabt::Index num_args,
    wabt::interp::TypedValue* args,
    wabt::Index num_results,
    wabt::interp::TypedValue* out_results,
    void* user_data
  );

private:
  // These assume that m_wasmMemory was set prior to execution.
  size_t memorySize() const override { return m_wasmMemory->data.size(); }
  void memorySet(size_t offset, uint8_t value) override { m_wasmMemory->data[offset] = static_cast<char>(value); }
  uint8_t memoryGet(size_t offset) override { return static_cast<uint8_t>(m_wasmMemory->data[offset]); }

  wabt::interp::Memory* m_wasmMemory;
};

class WabtEngine : public WasmEngine {
public:

  /// Factory method to create the WABT Wasm Engine.
  static std::unique_ptr<WasmEngine> create();

  ExecutionResult execute(
    evmc_context* context,
    std::vector<uint8_t> const& code,
    std::vector<uint8_t> const& state_code,
    evmc_message const& msg,
    bool meterInterfaceGas
  ) override;
};

}
