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
#include "shell-interface.h"
#include "hera.h"

using namespace wasm;

namespace HeraVM {

class OutOfGasException : std::exception {
public:
  const char* what() const noexcept override { return "Out of gas."; }
};

class InternalErrorException : std::runtime_error {
public:
  InternalErrorException(std::string msg): std::runtime_error(msg) {}
  const char* what() const noexcept override { return std::exception::what(); }
};

#define heraAssert(condition, msg) { \
  if (!(condition)) throw InternalErrorException(msg); \
}

struct EthereumInterface : ShellExternalInterface {
  EthereumInterface(Hera *_hera, HeraCall *_call) : ShellExternalInterface(), hera(_hera), call(_call) { }

  Literal callImport(Import *import, LiteralList& arguments) override;

  void trap(const char* why) override {
    throw InternalErrorException(std::string("Trap condition: ") + why);
  }

private:

  void takeGas(uint32_t gas)
  {
      if (gas > call->gas) {
        throw OutOfGasException();
      }

      call->gas -= gas;
  }

  void memoryCopy(std::vector<char> src, uint32_t srcoffset, uint32_t dstoffset, uint32_t length)
  {
    heraAssert((srcoffset + length) > srcoffset, "Out of bounds (source) memory copy.");
    heraAssert(src.size() < (srcoffset + length), "Out of bounds (source) memory copy.");
    heraAssert((dstoffset + length) > dstoffset, "Out of bounds (destination) memory copy.");

    uint32_t i = srcoffset;
    uint32_t j = dstoffset;

    for (; i < (srcoffset + length); i++, j++) {
      memory.set<uint8_t>(j, src[i]);
    }
  }

  void copyAddressToMemory(struct evm_address address, uint32_t dstoffset)
  {
    for (uint32_t i = 0, j = dstoffset; j < (dstoffset + 20); i++, j++) {
      memory.set<uint8_t>(j, address.bytes[i]);
    }
  }

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
  }

  void storeUint256(uint32_t dstOffset, struct evm_uint256be *src)
  {
      assert(src != nullptr);
      storeMemory(dstOffset, src->bytes, 32);
  }

  void loadUint160(uint32_t srcOffset, struct evm_address *dst)
  {
      assert(dst != nullptr);
      loadMemory(srcOffset, dst->bytes, 20);
  }

  void storeUint160(uint32_t dstOffset, struct evm_address *src)
  {
      assert(src != nullptr);
      storeMemory(dstOffset, src->bytes, 20);
  }

  /*
   * Utilities
   */

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
  Hera *hera;
  HeraCall *call;
};

}
