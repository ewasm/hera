/*
 * Hera VM: eWASM virtual machine conforming to the Ethereum VM C API
 *
 * Copyright (c) 2016 Alex Beregszaszi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
  ExecutionResult() { }
  ExecutionResult(uint64_t _gasLeft):
    gasLeft(_gasLeft)
  { }
  uint64_t gasLeft = 0;
  std::vector<uint8_t> returnValue;
  bool isRevert = false;
};

/* Base class for EEI implementations */
class EEI {
public:
  EEI(evmc_context *_context,
    std::vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas) :
    context(_context),
    code(_code),
    msg(_msg),
    result(_result),
    meterGas(_meterGas)
    { }

protected:
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
 * Generic EEI function prototypes
 */
  void eth_useGas(uint64_t gas);
  uint64_t eth_getGasLeft();

  void eth_getAddress(uint32_t resultOffset);
  void eth_getBalance(uint32_t addressOffset, uint32_t resultOffset);
  void eth_getBlockHash(int64_t number, uint32_t resultOffset);

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

/*
 * Helper functions (virtual)
 * FIXME: Having each WASM engine's EEI implement each of these requires more work on specification. Maybe retire some of these
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

  void ensureSenderBalance(evmc_uint256be const& value);

  uint64_t safeLoadUint128(evmc_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT64_MAX */
  bool exceedsUint64(evmc_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  bool exceedsUint128(evmc_uint256be const& value);

  /* Checks if 256 bit value is all zeroes */
  bool isZeroUint256(evmc_uint256be const& value);

  virtual uint8_t memory_getbyte(uint32_t offset) = 0;
  virtual void memory_setbyte(uint32_t offset, uint8_t val) = 0;
  virtual size_t memory_size() = 0;

  evmc_context *context;
  std::vector<uint8_t> const& code;
  evmc_message const& msg;
  std::vector<uint8_t> lastReturnData;
  ExecutionResult & result;
  bool meterGas;
};

struct BinaryenEEI : ShellExternalInterface, public EEI {
  BinaryenEEI(
    evmc_context* _context,
    std::vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    ShellExternalInterface(),
    EEI(_context, _code, _msg, _result, _meterGas)
  { }

  Literal callImport(Import *import, LiteralList& arguments) override;
#if HERA_DEBUGGING
  Literal callDebugImport(Import *import, LiteralList& arguments);
#endif

  void importGlobals(std::map<Name, Literal>& globals, Module& wasm) override;

  void trap(const char* why) override {
    throw InternalErrorException(std::string("Trap condition: ") + why);
  }

private:
  uint8_t memory_getbyte(uint32_t offset) override;
  void memory_setbyte(uint32_t offset, uint8_t val) override;
  size_t memory_size() override;
};

}
