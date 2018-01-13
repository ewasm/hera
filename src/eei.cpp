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

using namespace std;
using namespace wasm;

namespace HeraVM {

Literal EthereumInterface::callImport(Import *import, LiteralList& arguments) {
    heraAssert(import->module == Name("ethereum"), "Only imports from the 'ethereum' namespace are allowed.");

    if (import->base == Name("useGas")) {
      cout << "usegas ";

      uint64_t gas = arguments[0].geti64();

      cout << gas << "\n";

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getGasLeft")) {
      return Literal(result.gasLeft);
    }

    if (import->base == Name("getAddress")) {
      cout << "getAddress ";

      uint32_t resultOffset = arguments[0].geti32();

      cout << resultOffset << "\n";

      storeUint160(msg.address, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      std::cout << "getbalance\n";

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      evm_address address = loadUint160(addressOffset);
      evm_uint256be result;
      context->fn_table->get_balance(&result, context, &address);
      storeUint128(result, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallDataSize")) {
      cout << "calldatasize " << msg.input_size << "\n";
      return Literal((uint32_t)msg.input_size);
    }

    if (import->base == Name("callDataCopy")) {
      cout << "calldatacopy ";

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      cout << resultOffset << " " << dataOffset << " " << length << "\n";

      vector<uint8_t> input(msg.input, msg.input + msg.input_size);
      storeMemory(input, dataOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getCaller")) {
      cout << "getcaller\n";

      uint32_t resultOffset = arguments[0].geti32();

      storeUint160(msg.sender, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallValue")) {
      cout << "getcallvalue" << endl;

      uint32_t resultOffset = arguments[0].geti32();

      storeUint128(msg.value, resultOffset);

      return Literal();
    }

    if (import->base == Name("codeCopy")) {
      cout << "codecopy" << endl;

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t codeOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      storeMemory(code, codeOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getCodeSize")) {
      cout << "codesize" << endl;

      return Literal((uint32_t)code.size());
    }

    if (import->base == Name("getBlockCoinbase")) {
      cout << "getblockcoinbase" << endl;

      uint32_t resultOffset = arguments[0].geti32();

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint160(tx_context.block_coinbase, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockDifficulty")) {
      std::cout << "getblockdifficulty" << endl;

      uint32_t offset = arguments[0].geti32();

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint256(tx_context.block_difficulty, offset);

      return Literal();
    }

    if (import->base == Name("return") || import->base == Name("revert")) {
      cout << "return ";

      uint32_t offset = arguments[0].geti32();
      uint32_t size = arguments[1].geti32();

      cout << offset << " " << size << "\n";

      result.returnValue.clear();
      for (uint32_t i = offset; i < offset + size; i++) {
        result.returnValue.push_back(memory.get<uint8_t>(i));
      }

      if (import->base == Name("revert")) {
        result.isRevert = true;
      }

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str);
  }

  void EthereumInterface::takeGas(uint64_t gas)
  {
    if (gas > result.gasLeft) {
      throw OutOfGasException();
    }

    result.gasLeft -= gas;
  }

  /*
   * Memory Operations
   */

  void EthereumInterface::loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length)
  {
    heraAssert(length > 0, "Length must be nonzero");
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");

    for (uint32_t i = 0; i < length; ++i) {
      dst[length - (i + 1)] = memory.get<uint8_t>(srcOffset + i);
    }
  }

  void EthereumInterface::loadMemory(uint32_t srcOffset, vector<uint8_t> & dst, size_t length)
  {
    heraAssert(length > 0, "Length must be nonzero");
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");
    heraAssert(dst.size() >= length, "Out of bounds (destination) memory copy.");

    for (uint32_t i = 0; i < length; ++i) {
      dst[i] = memory.get<uint8_t>(srcOffset + i);
    }
  }

  void EthereumInterface::storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length)
  {
    heraAssert(length > 0, "Length must be nonzero");
    heraAssert((dstOffset + length) > dstOffset, "Out of bounds (destination) memory copy.");
    heraAssert(memory.size() < (dstOffset + length), "Out of bounds (destination) memory copy.");

    for (uint32_t i = 0; i < length; ++i) {
      memory.set<uint8_t>(dstOffset + length - (i + 1), src[i]);
    }
  }

  void EthereumInterface::storeMemory(vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length)
  {
    heraAssert(length > 0, "Length must be nonzero");
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");
    heraAssert(src.size() < (srcOffset + length), "Out of bounds (source) memory copy.");
    heraAssert((dstOffset + length) > dstOffset, "Out of bounds (destination) memory copy.");
    heraAssert(memory.size() < (dstOffset + length), "Out of bounds (destination) memory copy.");

    for (uint32_t i = 0; i < length; i++) {
      memory.set<uint8_t>(dstOffset + i, src[srcOffset + i]);
    }
  }

  /*
   * Memory Op Wrapper Functions
   */

  evm_uint256be EthereumInterface::loadUint256(uint32_t srcOffset)
  {
    evm_uint256be dst;
    loadMemory(srcOffset, dst.bytes, 32);
    return dst;
  }

  void EthereumInterface::storeUint256(evm_uint256be const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 32);
  }

  evm_address EthereumInterface::loadUint160(uint32_t srcOffset)
  {
    evm_address dst;
    loadMemory(srcOffset, dst.bytes, 20);
    return dst;
  }

  void EthereumInterface::storeUint160(evm_address const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 20);
  }

  evm_uint256be EthereumInterface::loadUint128(uint32_t srcOffset)
  {
    evm_uint256be dst;
    loadMemory(srcOffset, dst.bytes, 16);
    return dst;
  }

  void EthereumInterface::storeUint128(evm_uint256be const& src, uint32_t dstOffset)
  {
    heraAssert(!exceedsUint128(src), "Value at src cannot exceed 2^128-1");
    storeMemory(src.bytes + 16, dstOffset, 16);
  }

  /*
   * Utilities
   */

  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  unsigned int EthereumInterface::exceedsUint128(evm_uint256be const& value)
  {
    for (unsigned i = 0; i < 16; ++i) {
      if (value.bytes[i])
        return 1;
    }
    return 0;
  }
}
