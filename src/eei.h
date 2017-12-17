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
#include <shell-interface.h>
#include "hera.h"

using namespace wasm;

namespace HeraVM {

struct EthereumInterface : ShellExternalInterface {
  EthereumInterface(Hera *_hera, HeraCall *_call) : ShellExternalInterface(), hera(_hera), call(_call) { }

  Literal callImport(Import *import, LiteralList& arguments) override;

  void trap(const char* why) override {
    throw std::runtime_error(std::string("Trap condition: ") + why);
  }

private:

  void takeGas(uint32_t gas)
  {
      if (gas > call->gas) {
        throw std::runtime_error("Out of gas.");
      }

      call->gas -= gas;
  }

  void memoryCopy(std::vector<char> src, uint32_t srcoffset, uint32_t dstoffset, uint32_t length)
  {
    if (src.size() < (srcoffset + length)) {
      // FIXME: exception type
      throw std::runtime_error("Out of bounds memory copy.");
    }

    uint32_t i = srcoffset;
    uint32_t j = dstoffset;

    for (; i < (srcoffset + length); i++, j++) {
      memory.set<uint8_t>(j, src[i]);
    }
  }

  void copyAddressToMemory(struct evm_hash160 hash160, uint32_t dstoffset)
  {
    for (uint32_t i = 0, j = dstoffset; j < (dstoffset + 20); i++, j++) {
      memory.set<uint8_t>(j, hash160.bytes[i]);
    }
  }

private:
  Hera *hera;
  HeraCall *call;
};

}
