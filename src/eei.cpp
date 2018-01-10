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

#include <stdexcept>
#include "eei.h"

using namespace wasm;

namespace HeraVM {

Literal EthereumInterface::callImport(Import *import, LiteralList& arguments) {
    heraAssert(import->module == Name("ethereum"), "Only imports from the 'ethereum' namespace are allowed.");

    if (import->base == Name("useGas")) {
      std::cout << "usegas ";

      uint32_t gas = arguments[0].geti32();

      std::cout << gas << "\n";

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getAddress")) {
      std::cout << "getAddress ";

      uint32_t resultOffset = arguments[0].geti32();

      std::cout << resultOffset << "\n";

      copyAddressToMemory(call->msg->address, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallDataSize")) {
      std::cout << "calldatasize " << call->msg->input_size << "\n";
      return Literal((uint32_t)call->msg->input_size);
    }

    if (import->base == Name("callDataCopy")) {
      std::cout << "calldatacopy ";

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      std::cout << resultOffset << " " << dataOffset << " " << length << "\n";

      std::vector<char> input(call->msg->input, call->msg->input + call->msg->input_size);
      memoryCopy(input, dataOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("return")) {
      std::cout << "return ";

      uint32_t offset = arguments[0].geti32();
      uint32_t size = arguments[1].geti32();

      std::cout << offset << " " << size << "\n";

      call->returnValue.clear();
      for (uint32_t i = offset; i < offset + size; i++) {
        call->returnValue.push_back(memory.get<uint8_t>(i));
      }

      return Literal();
    }

    heraAssert(false, std::string("Unsupported import called: ") + import->module.str + "::" + import->base.str);
  }

  void EthereumInterface::takeGas(uint32_t gas)
  {
    if (gas > call->gas) {
      throw OutOfGasException();
    }

    call->gas -= gas;
  }

  void EthereumInterface::memoryCopy(std::vector<char> const& src, uint32_t srcoffset, uint32_t dstoffset, uint32_t length)
  {
    heraAssert((srcoffset + length) > srcoffset, "Out of bounds (source) memory copy.");
    heraAssert(src.size() < (srcoffset + length), "Out of bounds (source) memory copy.");
    heraAssert((dstoffset + length) > dstoffset, "Out of bounds (destination) memory copy.");
    heraAssert(memory.size() < (dstoffset + length), "Out of bounds (destination) memory copy.");

    uint32_t i = srcoffset;
    uint32_t j = dstoffset;

    for (; i < (srcoffset + length); i++, j++) {
      memory.set<uint8_t>(j, src[i]);
    }
  }

  void EthereumInterface::copyAddressToMemory(struct evm_address const& address, uint32_t dstoffset)
  {
    heraAssert(memory.size() < (dstoffset + 20), "Out of bounds (destination) memory copy.");
    for (uint32_t i = 0, j = dstoffset; j < (dstoffset + 20); i++, j++) {
      memory.set<uint8_t>(j, address.bytes[i]);
    }
  }
}
