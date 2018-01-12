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

class OutOfGasException : std::exception {
public:
  const char* what() const noexcept override { return "Out of gas."; }
};

class InternalErrorException : std::exception {
public:
  InternalErrorException(std::string _msg): msg(_msg) {}
  const char* what() const noexcept override { return const_cast<char*>(msg.c_str()); }
private:
  std::string msg;
};

#define heraAssert(condition, msg) { \
  if (!(condition)) throw InternalErrorException(msg); \
}

struct ExecutionResult {
  uint64_t gasLeft;
  std::vector<char> returnValue;
  bool isRevert;
};

struct EthereumInterface : ShellExternalInterface {
  EthereumInterface(
    struct evm_context const& _context,
    struct evm_message const& _msg,
    ExecutionResult & _result
  ):
    ShellExternalInterface(),
    context(_context),
    msg(_msg),
    result(_result)
  { (void)context; }

  Literal callImport(Import *import, LiteralList& arguments) override;

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

  unsigned int exceedsUint128(evm_uint256be const& value);

  struct evm_context const& context;
  struct evm_message const& msg;
  ExecutionResult result;
};

}
