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

      const uint32_t resultOffset = arguments[0].geti32();

      cout << resultOffset << "\n";

      storeUint160(resultOffset, msg.address);

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      std::cout << "getbalance\n";

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      evm_address *address = new evm_address;
      evm_uint256be *result = new evm_uint256be;

      loadUint160(addressOffset, address);
      hera->context->fn_table->get_balance(result, hera->context, address);
      storeUint128(resultOffset, result);

      delete address;
      delete result;

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      std::cout << "getblockhash " << number << "\n";

      int64_t number = arguments[0].geti64();
      uint32_t resultOffset = arguments[1].geti32();

      evm_uint256be *blockhash = new evm_uint256be;
      hera->context->fn_table->get_block_hash(blockhash, hera->context, number);
      storeUint256(resultOffset, blockhash);

      delete blockhash;

      return Literal();
    }

    if (import->base == Name("call")) {
      std::cout << "call\n";

      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t valueOffset = arguments[2].geti32();
      uint32_t dataOffset = arguments[3].geti32();
      int32_t dataLength = arguments[4].geti32();
      uint32_t resultOffset = arguments[5].geti32();
      int32_t resultLength = arguments[6].geti32();

      evm_result *call_result = new evm_result;
      evm_message *call_message = new evm_message;
      call_message->input = new uint8_t[dataLength];
      unsigned int vm_status = 0;
      
      loadUint160(addressOffset, call_message->address);
      call_message->sender = call->address;
      loadUint128(valueOffset, call_message->value);
      loadMemory(dataOffset, call_message->input, dataLength);
      call_message->input_size = dataLength;
      std::memset(call_message->code_hash, 0, sizeof(struct evm_uint256be));
      call_message->gas = gas;
      call_message->depth = ++call->msg->depth;
      call_message->kind = 0;
      call_message->flags = 1;

      hera->context->fn_table->call(call_result, hera->context, call_message);

      storeMemory(resultOffset, call_result->output_data, resultLength);
      vm_status = call_result->status_code;
     
      delete call_message->input;
      delete call_message;
      if (call_result->release != nullptr)
        call_result->release(call_result);
      
      return Literal((int32_t)status_code);
    }

    if (import->base == Name("getCallDataSize")) {
      cout << "calldatasize " << msg.input_size << "\n";
      return Literal((uint32_t)msg.input_size);
    }

    if (import->base == Name("callDataCopy")) {
      cout << "calldatacopy ";

      const uint32_t resultOffset = arguments[0].geti32();
      const uint32_t dataOffset = arguments[1].geti32();
      const uint32_t length = arguments[2].geti32();

      cout << resultOffset << " " << dataOffset << " " << length << "\n";

      vector<char> input(msg.input, msg.input + msg.input_size);
      memoryCopy(input, dataOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("return") || import->base == Name("revert")) {
      cout << "return ";

      const uint32_t offset = arguments[0].geti32();
      const uint32_t size = arguments[1].geti32();

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

  void EthereumInterface::memoryCopy(vector<char> const& src, uint32_t srcoffset, uint32_t dstoffset, uint32_t length)
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
  
  /*
   * Memory Operations
   */

  void EthereumInterface::loadMemory(const uint32_t srcOffset, uint8_t *dst, size_t length)
  {
      heraAssert(length > 0, "Length must be nonzero");
      heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");

      for (uint32_t i = 0; i < length; ++i) {
          dst[length - (i + 1)] = memory.get<uint8_t>(srcOffset + i);
      }
  }

  void EthereumInterface::storeMemory(const uint32_t dstOffset, const uint8_t *src, size_t length)
  {
      heraAssert(length > 0, "Length must be nonzero");
      heraAssert((dstOffset + length) > dstOffset, "Out of bounds (destination) memory copy.");
      heraAssert(memory.size() < (dstOffset + length), "Out of bounds (destination) memory copy.");

      for (uint32_t i = 0; i < length; ++i) {
          memory.set<uint8_t>(dstOffset + length - (i + 1), *(src + i));
      }
  }

  /*
   * Memory Op Wrapper Functions
   */

  evm_uint256be EthereumInterface::loadUint256(const uint32_t srcOffset)
  {
      evm_uint256be dst;
      loadMemory(srcOffset, dst.bytes, 32);
      return dst;
  }

  void EthereumInterface::storeUint256(const uint32_t dstOffset, const evm_uint256be &src)
  {
      storeMemory(dstOffset, src.bytes, 32);
  }

  evm_address EthereumInterface::loadUint160(const uint32_t srcOffset)
  {
      evm_address dst;
      loadMemory(srcOffset, dst.bytes, 20);
      return dst;
  }

  void EthereumInterface::storeUint160(const uint32_t dstOffset, const evm_address &src)
  {
      storeMemory(dstOffset, src.bytes, 20);
  }

  evm_uint256be EthereumInterface::loadUint128(const uint32_t srcOffset)
  {
      evm_uint256be dst;
      loadMemory(srcOffset, dst.bytes, 16);
      return dst;
  }

  void EthereumInterface::storeUint128(const uint32_t dstOffset, const evm_uint256be &src)
  {
      heraAssert(!exceedsUint128(src), "Value at src cannot exceed 2^128-1");
      storeMemory(dstOffset, &src.bytes[16], 16);
  }

  /*
   * Utilities
   */

  /* Checks if host supplied 256 bit value exceeds UINT128_MAX */
  unsigned int EthereumInterface::exceedsUint128(const evm_uint256be &value)
  {
      for (int i = 0; i < 16; ++i) {
        if (value.bytes[i])
	    return 1;
      }
      return 0;
  }

  /* Endianness Converter */
  void EthereumInterface::endianSwap(uint8_t *bytes, const size_t length)
  {
  	heraAssert(length > 0, "Length must be nonzero.");

  	size_t i = 0;
	size_t j = length - 1;

	while (i < j) {
		bytes[i] ^= bytes[j];
		bytes[j] ^= bytes[i];
		bytes[i] ^= bytes[j];

		++i;
		--j;
	}
  }
}
