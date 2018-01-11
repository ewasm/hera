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
  void memoryCopy(std::vector<char> const& src, uint32_t srcoffset, uint32_t dstoffset, uint32_t length);

  void loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length);
  void storeMemory(uint32_t dstOffset, uint8_t *src, size_t length);

  void loadUint256(uint32_t srcOffset, struct evm_uint256be *dst);
  void storeUint256(uint32_t dstOffset, struct evm_uint256be *src);
  void loadUint160(uint32_t srcOffset, struct evm_address *dst);
  void storeUint160(uint32_t dstOffset, struct evm_address *src);
  void loadUint128(uint32_t srcOffset, struct evm_uint256be *dst);
  void storeUint128(uint32_t dstOffset, struct evm_uint256be *src);

  unsigned int exceedsUint128(struct evm_uint256be *value);
  void endianSwap(uint8_t *bytes, size_t len);

private:
  struct evm_context const& context;
  struct evm_message const& msg;
  ExecutionResult result;
};

}
