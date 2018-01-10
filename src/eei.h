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
  void copyAddressToMemory(struct evm_address const& address, uint32_t dstoffset);

  /*
   * Memory Operations
   */
  
  void loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length)
  {
      assert(dst != nullptr);
      assert(length);
      
      for (uint32_t i = 0; i < length; ++i) {
          *(dst + i) = memory.get<uint8_t>(srcOffset + i);
      }
  }

  void storeMemory(uint32_t dstOffset, uint8_t *src, size_t length)
  {
      assert(src != nullptr);
      assert(length);

      for (uint32_t i = 0; i < length; ++i) {
          memory.set<uint8_t>(dstOffset + i, *(src + i));
      }
  }

  /*
   * Memory Op Wrapper Functions
   */

  void loadUint256(uint32_t srcOffset, struct evm_uint256be *dst)
  {
      assert(dst != nullptr);
      loadMemory(srcOffset, dst->bytes, 32);
      endianSwap(dst->bytes, 32);
  }

  void storeUint256(uint32_t dstOffset, struct evm_uint256be *src)
  {
      assert(src != nullptr);
      endianSwap(src->bytes, 32);
      storeMemory(dstOffset, src->bytes, 32);
  }

  void loadUint160(uint32_t srcOffset, struct evm_address *dst)
  {
      assert(dst != nullptr);
      loadMemory(srcOffset, dst->bytes, 20);
      endianSwap(dst->bytes, 20);
  }

  void storeUint160(uint32_t dstOffset, struct evm_address *src)
  {
      assert(src != nullptr);
      endianSwap(src->bytes, 20);
      storeMemory(dstOffset, src->bytes, 20);
  }

  void loadUint128(uint32_t srcOffset, struct evm_uint256be *dst)
  {
      assert(dst != nullptr);
      loadMemory(srcOffset, dst->bytes, 16);
      endianSwap(dst->bytes, 32);
  }

  void storeUint128(uint32_t dstOffset, struct evm_uint256be *src)
  {
      assert(src != nullptr);
      assert(!exceedsUint128(src));
      endianSwap(src->bytes, 32);
      storeMemory(dstOffset, src->bytes, 16);
  }

  /*
   * Utilities
   */
  
  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  unsigned int exceedsUint128(struct evm_uint256be *value)
  {
      assert(value != nullptr);
      for (int i = 0; i < 16; ++i) {
        if (value->bytes[i])
	    return 1;
      }
      return 0;
  }

  /* Endianness Converter */
  void endianSwap(uint8_t *bytes, size_t len)
  {
  	assert(len);

  	int i = 0;
	int j = len - 1;

	while (i < j) {
		bytes[i] ^= bytes[j];
		bytes[j] ^= bytes[i];
		bytes[i] ^= bytes[j];

		++i;
		--j;
	}
  }
private:
  struct evm_context const& context;
  struct evm_message const& msg;
  ExecutionResult result;
};

}
