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
#include "evm.h"
#include "shell-interface.h"
#include "hera.h"

using namespace wasm;

namespace HeraVM {

class OutOfGasException : public std::exception {
public:
  const char* what() const noexcept override { return "Out of gas."; }
};

/// Static Mode Violation.
///
/// This exception is thrown when state modifying EEI function is called
/// in static mode.
class StaticModeViolation : public std::exception {
public:
  explicit StaticModeViolation(std::string const& _functionName):
    msg("Static mode violation in " + _functionName + ".")
  {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

class InternalErrorException : public std::exception {
public:
  explicit InternalErrorException(std::string _msg): msg(std::move(_msg)) {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

#define heraAssert(condition, msg) { \
  if (!(condition)) throw InternalErrorException(msg); \
}

struct ExecutionResult {
  uint64_t gasLeft = 0;
  std::vector<uint8_t> returnValue;
  bool isRevert = false;
};

struct EthereumInterface : ShellExternalInterface {
  EthereumInterface(
    evm_context* _context,
    std::vector<uint8_t> const& _code,
    evm_message const& _msg,
    ExecutionResult & _result
  ):
    ShellExternalInterface(),
    context(_context),
    code(_code),
    msg(_msg),
    result(_result)
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
  void takeGas(uint64_t gas);

  void loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length);
  void loadMemory(uint32_t srcOffset, std::vector<uint8_t> & dst, size_t length);
  void storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length);
  void storeMemory(std::vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length);

  evm_uint256be loadUint256(uint32_t srcOffset);
  void storeUint256(evm_uint256be const& src, uint32_t dstOffset);
  evm_address loadUint160(uint32_t srcOffset);
  void storeUint160(evm_address const& src, uint32_t dstOffset);
  evm_uint256be loadUint128(uint32_t srcOffset);
  void storeUint128(evm_uint256be const& src, uint32_t dstOffset);

  void ensureSenderBalance(evm_uint256be const& value);

  static uint64_t safeLoadUint64(evm_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT64_MAX */
  static bool exceedsUint64(evm_uint256be const& value);

  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  static bool exceedsUint128(evm_uint256be const& value);

  /* Checks if 256 bit value is all zeroes */
  static bool isZeroUint256(evm_uint256be const& value);

  evm_context* context = nullptr;
  std::vector<uint8_t> const& code;
  evm_message const& msg;
  std::vector<uint8_t> lastReturnData;
  ExecutionResult & result;
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
  static constexpr unsigned callNewAccount = 25000;
};

}
