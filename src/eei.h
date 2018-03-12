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

/*
 * Execution output data structure. Used in WasmEngine class
 */
struct ExecutionResult {
  ExecutionResult() { }
  ExecutionResult(uint64_t _gasLeft):
    gasLeft(_gasLeft)
  { }
  uint64_t gasLeft = 0;
  std::vector<uint8_t> returnValue;
  bool isRevert = false;
};

/* 
 * Base class for EEI implementations.
 * Children of this class implement WASM engine-specific code for byte memory access
 *   and passing EEI arguments and returns between the WASM module and the abstract interface.
 *
 *   See eei.cpp for example
 */
class EEI {
public:
  EEI(evmc_context *_context,
    std::vector<uint8_t> const& _code,
    std::vector<uint8_t> const& _state_code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas) :
    context(_context),
    code(_code),
    state_code(_state_code),
    msg(_msg),
    result(_result),
    meterGas(_meterGas)
    { }

protected:
/*
 * Gas charge schedule.
 */
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
    static constexpr unsigned callNewAccount = 25000;
  };

/*
 * EEI host functions.
 * WASM-engine specific code is only responsible for validating imports,
 * passing arguments to these functions, and passing their return values.
 *
 * See https://github.com/ewasm/design/blob/master/eth_interface.md
 */
  void eth_useGas(uint64_t gas);
  uint64_t eth_getGasLeft();

  void eth_getAddress(uint32_t resultOffset);
  void eth_getBalance(uint32_t addressOffset, uint32_t resultOffset);
  void eth_getBlockHash(int64_t number, uint32_t resultOffset);

  virtual void trap(const char* why) = 0;

  uint32_t eth_call(
    int64_t gas, 
    uint32_t addressOffset,
    uint32_t valueOffset,
    uint32_t dataOffset,
    uint32_t dataLength);
  uint32_t eth_callCode(
    int64_t gas,
    uint32_t addressOffset,
    uint32_t valueOffset,
    uint32_t dataOffset,
    uint32_t dataLength);
  uint32_t eth_callDelegate(
    int64_t gas,
    uint32_t addressOffset,
    uint32_t dataOffset,
    uint32_t dataLength);
  uint32_t eth_callStatic(
    int64_t gas,
    uint32_t addressOffset,
    uint32_t dataOffset,
    uint32_t dataLength);
  uint32_t eth_create(
    uint32_t valueOffset,
    uint32_t dataOffset,
    uint32_t length,
    uint32_t resultOffset);

  void eth_callDataCopy(
    uint32_t resultOffset,
    uint32_t dataOffset,
    uint32_t length);
  uint32_t eth_getCallDataSize();

  void eth_codeCopy(uint32_t resultOffset, uint32_t codeOffset, uint32_t length);
  uint32_t eth_getCodeSize();

  void eth_externalCodeCopy(
    uint32_t addressOffset,
    uint32_t resultOffset,
    uint32_t codeOffset,
    uint32_t length);
  uint32_t eth_getExternalCodeSize(uint32_t addressOffset);
  
  void eth_getCaller(uint32_t resultOffset);
  void eth_getCallValue(uint32_t resultOffset);
  
  void eth_getBlockDifficulty(uint32_t offset);
  void eth_getBlockCoinbase(uint32_t resultOffset);
  int64_t eth_getBlockNumber();
  int64_t eth_getBlockGasLimit();
  int64_t eth_getBlockTimestamp();

  void eth_getTxGasPrice(uint32_t valueOffset);
  void eth_getTxOrigin(uint32_t resultOffset);

  void eth_storageStore(uint32_t pathOffset, uint32_t valueOffset);
  void eth_storageLoad(uint32_t pathOffset, uint32_t resultOffset);

  void eth_log(
    uint32_t dataOffset,
    uint32_t length,
    uint32_t numberOfTopics,
    uint32_t topic1,
    uint32_t topic2,
    uint32_t topic3,
    uint32_t topic4);

  uint32_t eth_getReturnDataSize();
  void eth_returnDataCopy(uint32_t resultOffset,
    uint32_t dataOffset,
    uint32_t length);

  void eth_return(uint32_t dataOffset, uint32_t length);
  void eth_revert(uint32_t dataOffset, uint32_t length);

  void eth_selfDestruct(uint32_t addressOffset);

  void takeInterfaceGas(uint64_t gas);

/*
 * Memory access helper functions usable by any WASM engine.
 * Dependent on memory_getbyte, memory_setbyte, and memory_size virtual methods.
 */
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

/*
 * Arithmetic checks. 
 */
  void ensureSenderBalance(evmc_uint256be const& value);

  unsigned __int128 safeLoadUint128(evmc_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT64_MAX */
  bool exceedsUint64(evmc_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  bool exceedsUint128(evmc_uint256be const& value);

  /* Checks if 256 bit value is all zeroes */
  bool isZeroUint256(evmc_uint256be const& value);

/*
 * Pure virtual methods for writing/reading bytes in memory. 
 * Must be implemented for each WASM engine.
 * Used in high-level memory access helpers.
 */
  virtual uint8_t memory_getbyte(uint32_t offset) = 0;
  virtual void memory_setbyte(uint32_t offset, uint8_t val) = 0;
  virtual size_t memory_size() = 0;

  evmc_context *context;
  std::vector<uint8_t> const& code;
  std::vector<uint8_t> const& state_code;
  evmc_message const& msg;
  std::vector<uint8_t> lastReturnData;
  ExecutionResult & result;
  bool meterGas;
};

/*
 * Binaryen EEI object.
 * Inherits abstract EEI interface and implements some Binaryen callbacks. 
 */
struct BinaryenEEI : ShellExternalInterface, public EEI {
  BinaryenEEI(
    evmc_context* _context,
    std::vector<uint8_t> const& _code,
    std::vector<uint8_t> const& _state_code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    ShellExternalInterface(),
    EEI(_context, _code, _state_code, _msg, _result, _meterGas)
  { }

  Literal callImport(Import *import, LiteralList& arguments) override;
#if HERA_DEBUGGING
  Literal callDebugImport(Import *import, LiteralList& arguments);
#endif

  void importGlobals(std::map<Name, Literal>& globals, Module& wasm) override;

  void trap(const char* why) override {
    ensureCondition(false, OutOfGas, std::string("Trap condition: ") + why);
  }

private:
  uint8_t memory_getbyte(uint32_t offset) override;
  void memory_setbyte(uint32_t offset, uint8_t val) override;
  size_t memory_size() override;
};

}
