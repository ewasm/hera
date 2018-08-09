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
#include <evmc/evmc.h>
#include "shell-interface.h"
#include "hera.h"
#include "exceptions.h"

using namespace wasm;

namespace HeraVM {

struct ExecutionResult {
  uint64_t gasLeft = 0;
  std::vector<uint8_t> returnValue;
  bool isRevert = false;
};

struct EthereumInterface : ShellExternalInterface {
  EthereumInterface(
    evmc_context* _context,
    std::vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    ShellExternalInterface(),
    context(_context),
    code(_code),
    msg(_msg),
    result(_result),
    meterGas(_meterGas)
  {
    // set starting gas
    result.gasLeft = static_cast<uint64_t>(msg.gas);
    // set sane defaults
    result.returnValue = std::vector<uint8_t>{};
    result.isRevert = false;
  }

  Literal callImport(Import *import, LiteralList& arguments) override;
#if HERA_DEBUGGING
  Literal callDebugImport(Import *import, LiteralList& arguments);
#endif

  void importGlobals(std::map<Name, Literal>& globals, Module& wasm) override;

  void trap(const char* why) override {
    // TODO: use a specific error code here?
    ensureCondition(false, OutOfGas, std::string("Trap condition: ") + why);
  }

private:
  enum class EEICallKind {
    Call,
    CallCode,
    CallDelegate,
    CallStatic
  };

  void takeGas(uint64_t gas);
  void takeInterfaceGas(uint64_t gas);

  void ensureSourceMemoryBounds(uint32_t offset, uint32_t length);
  void loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length);
  void loadMemory(uint32_t srcOffset, std::vector<uint8_t> & dst, size_t length);
  void storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length);
  void storeMemory(std::vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length);

  evmc_uint256be loadUint256(uint32_t srcOffset);
  void storeUint256(evmc_uint256be const& src, uint32_t dstOffset);
  evmc_address loadUint160(uint32_t srcOffset);
  void storeUint160(evmc_address const& src, uint32_t dstOffset);
  evmc_uint256be loadUint128(uint32_t srcOffset);
  void storeUint128(evmc_uint256be const& src, uint32_t dstOffset);

  bool enoughSenderBalanceFor(evmc_uint256be const& value) const;

  static unsigned __int128 safeLoadUint128(evmc_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT64_MAX */
  static bool exceedsUint64(evmc_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  static bool exceedsUint128(evmc_uint256be const& value);

  /* Checks if 256 bit value is all zeroes */
  static bool isZeroUint256(evmc_uint256be const& value);

  evmc_context* context = nullptr;
  std::vector<uint8_t> const& code;
  evmc_message const& msg;
  std::vector<uint8_t> lastReturnData;
  ExecutionResult & result;
  bool meterGas = true;
};

struct GasSchedule {
  static constexpr unsigned storageLoad = 200;
  static constexpr unsigned storageStoreCreate = 20000;
  static constexpr unsigned storageStoreChange = 5000;
  static constexpr unsigned log = 375;
  static constexpr unsigned logData = 8;
  static constexpr unsigned logTopic = 375;
  static constexpr unsigned create = 32000;
  static constexpr unsigned call = 700;
  static constexpr unsigned copy = 3;
  static constexpr unsigned blockhash = 800;
  static constexpr unsigned balance = 400;
  static constexpr unsigned base = 2;
  static constexpr unsigned verylow = 3;
  static constexpr unsigned extcode = 700;
  static constexpr unsigned selfdestruct = 5000;
  static constexpr unsigned valuetransfer = 9000;
  static constexpr unsigned valueStipend = 2300;
  static constexpr unsigned callNewAccount = 25000;
};

}
