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

#include <chrono>
#include <cstdint>
#include <string>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include "exceptions.h"
#include "helpers.h"

namespace hera {

struct ExecutionResult {
  int64_t gasLeft = 0;
  bytes returnValue;
  bool isRevert = false;
};

// There is a single engine instance in each VM instance and
// likely execute() is called multiple times. As a result
// an engine implementation cannot have instance variables with
// side-effects.
class WasmEngine {
public:
  virtual ~WasmEngine() noexcept = default;

  virtual ExecutionResult execute(
    evmc::HostContext& context,
    bytes_view code,
    bytes_view state_code,
    evmc_message const& msg,
    bool meterInterfaceGas
  ) = 0;

  virtual void verifyContract(bytes_view code) = 0;

  static void enableBenchmarking() noexcept { benchmarkingEnabled = true; }

protected:
  void instantiationStarted() noexcept
  {
    if (benchmarkingEnabled)
      instantiationStartTime = clock::now();
  }

  void executionStarted() noexcept
  {
    if (benchmarkingEnabled)
      executionStartTime = clock::now();
  }

  void executionFinished()
  {
    if (benchmarkingEnabled)
      collectBenchmarkingData();
  }

private:
  void collectBenchmarkingData();

  using clock = std::chrono::high_resolution_clock;
  static bool benchmarkingEnabled;
  clock::time_point instantiationStartTime;
  clock::time_point executionStartTime;
};

class EthereumInterface {
public:
  explicit EthereumInterface(
    evmc::HostContext& _host,
    bytes_view _code,
    evmc_message const& _msg,
    ExecutionResult& _result,
    bool _meterGas
  ):
    m_host{_host}, // FIXME: Change param to &.
    m_code{_code},
    m_msg(_msg),
    m_result(_result),
    m_meterGas(_meterGas)
  {
    heraAssert((m_msg.flags & ~uint32_t(EVMC_STATIC)) == 0, "Unknown flags not supported.");

    // set starting gas
    m_result.gasLeft = m_msg.gas;
    // set sane defaults
    m_result.returnValue = {};
    m_result.isRevert = false;
  }

// WAVM/WABT host functions access this interface through an instance,
// which requires public methods.
// TODO: update upstream WAVM/WABT to have a context (user data) passed down.
#if HERA_WAVM == 0 && HERA_WABT == 0 && HERA_WASMER == 0
protected:
#endif
  virtual size_t memorySize() const = 0 ;
  virtual void memorySet(size_t offset, uint8_t value) = 0;
  virtual uint8_t memoryGet(size_t offset) = 0;
  virtual uint8_t* memoryPointer(size_t offset, size_t length) = 0;

  enum class EEICallKind {
    Call,
    CallCode,
    CallDelegate,
    CallStatic
  };

  // EEI methods

#if HERA_DEBUGGING
  void debugPrint32(uint32_t value);
  void debugPrint64(uint64_t value);
  void debugPrintMem(bool useHex, uint32_t offset, uint32_t length);
  void debugPrintStorage(bool useHex, uint32_t pathOffset);
  void debugEvmTrace(uint32_t pc, int32_t opcode, uint32_t cost, int32_t sp);
#endif

  void eeiUseGas(int64_t gas);
  int64_t eeiGetGasLeft();
  void eeiGetAddress(uint32_t resultOffset);
  void eeiGetExternalBalance(uint32_t addressOffset, uint32_t resultOffset);
  uint32_t eeiGetBlockHash(uint64_t number, uint32_t resultOffset);
  uint32_t eeiGetCallDataSize();
  void eeiCallDataCopy(uint32_t resultOffset, uint32_t dataOffset, uint32_t length);
  void eeiGetCaller(uint32_t resultOffset);
  void eeiGetCallValue(uint32_t resultOffset);
  void eeiCodeCopy(uint32_t resultOffset, uint32_t codeOffset, uint32_t length);
  uint32_t eeiGetCodeSize();
  void eeiExternalCodeCopy(uint32_t addressOffset, uint32_t resultOffset, uint32_t codeOffset, uint32_t length);
  uint32_t eeiGetExternalCodeSize(uint32_t addressOffset);
  void eeiGetBlockCoinbase(uint32_t resultOffset);
  void eeiGetBlockDifficulty(uint32_t offset);
  int64_t eeiGetBlockGasLimit();
  void eeiGetTxGasPrice(uint32_t valueOffset);
  void eeiLog(uint32_t dataOffset, uint32_t length, uint32_t numberOfTopics, uint32_t topic1, uint32_t topic2, uint32_t topic3, uint32_t topic4);
  int64_t eeiGetBlockNumber();
  int64_t eeiGetBlockTimestamp();
  void eeiGetTxOrigin(uint32_t resultOffset);
  void eeiStorageStore(uint32_t pathOffset, uint32_t valueOffset);
  void eeiStorageLoad(uint32_t pathOffset, uint32_t resultOffset);
  void eeiFinish(uint32_t offset, uint32_t size) { eeiRevertOrFinish(false, offset, size); }
  void eeiRevert(uint32_t offset, uint32_t size) { eeiRevertOrFinish(true, offset, size); }
  uint32_t eeiGetReturnDataSize();
  void eeiReturnDataCopy(uint32_t dataOffset, uint32_t offset, uint32_t size);
  uint32_t eeiCall(EEICallKind kind, int64_t gas, uint32_t addressOffset, uint32_t valueOffset, uint32_t dataOffset, uint32_t dataLength);
  uint32_t eeiCreate(uint32_t valueOffset, uint32_t dataOffset, uint32_t length, uint32_t resultOffset);
  void eeiSelfDestruct(uint32_t addressOffset);

private:
  void eeiRevertOrFinish(bool revert, uint32_t offset, uint32_t size);

  // Helpers methods
  inline std::string depthToString() const {
    return "[" + std::to_string(m_msg.depth) + "]";
  }

  void takeGas(int64_t gas);
  void takeInterfaceGas(int64_t gas);

  void ensureSourceMemoryBounds(uint32_t offset, uint32_t length);
  void loadMemoryReverse(uint32_t srcOffset, uint8_t *dst, size_t length);
  void loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length);
  void loadMemory(uint32_t srcOffset, bytes& dst, size_t length);
  void storeMemoryReverse(const uint8_t *src, uint32_t dstOffset, uint32_t length);
  void storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length);
  void storeMemory(bytes_view src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length);

  evmc::bytes32 loadBytes32(uint32_t srcOffset);
  void storeBytes32(evmc::bytes32 const& src, uint32_t dstOffset);
  evmc::uint256be loadUint256(uint32_t srcOffset);
  void storeUint256(evmc::uint256be const& src, uint32_t dstOffset);
  evmc::address loadAddress(uint32_t srcOffset);
  void storeAddress(evmc::address const& src, uint32_t dstOffset);
  evmc::uint256be loadUint128(uint32_t srcOffset);
  void storeUint128(evmc::uint256be const& src, uint32_t dstOffset);

  inline int64_t maxCallGas(int64_t gas) { return gas - (gas / 64); }

  /* Checks for overflow and safely charges gas for variable length data copies */
  void safeChargeDataCopy(uint32_t length, unsigned baseCost);

  bool enoughSenderBalanceFor(evmc::uint256be const& value);

  static unsigned __int128 safeLoadUint128(evmc::uint256be const& value);

  evmc::HostContext& m_host;
  bytes_view m_code;
  evmc_message const& m_msg;
  bytes m_lastReturnData;
  ExecutionResult & m_result;
  bool m_meterGas = true;
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
