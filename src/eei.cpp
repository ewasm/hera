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

#if HERA_DEBUGGING

namespace {

string to_string(evm_address const& address)
{
  stringstream s;
  s << hex;
  for (unsigned i = 0; i < 20; i++)
    s << static_cast<int>(address.bytes[i]);
  return s.str();
}

string to_string(evm_uint256be const& value)
{
  stringstream s;
  s << hex;
  for (unsigned i = 0; i < 32; i++)
    s << static_cast<int>(value.bytes[i]);
  return s.str();
}

}

#define HERA_DEBUG(msg) do { cout << msg << endl; } while(0)
#else
#define HERA_DEBUG(msg) do{ } while(0)
#endif

namespace HeraVM {

Literal EthereumInterface::callImport(Import *import, LiteralList& arguments) {
    heraAssert(import->module == Name("ethereum"), "Only imports from the 'ethereum' namespace are allowed.");

    if (import->base == Name("useGas")) {
      uint64_t gas = arguments[0].geti64();

      HERA_DEBUG(string("useGas < ") + to_string(gas));

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getGasLeft")) {
      HERA_DEBUG(string("getGasLeft out:") + to_string(result.gasLeft));
    
      return Literal(result.gasLeft);
    }

    if (import->base == Name("getAddress")) {
      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG(string("getAddress < ") + to_string(resultOffset));

      storeUint160(msg.address, resultOffset);

      HERA_DEBUG(string("getAddress > ") + to_string(msg.address));

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG(string("getBalance < ") + to_string(addressOffset) + " " + to_string(resultOffset));

      evm_address address = loadUint160(addressOffset);
      evm_uint256be result;
      context->fn_table->get_balance(&result, context, &address);
      storeUint128(result, resultOffset);

      HERA_DEBUG(string("getBalance > ") + to_string(address) + " " + to_string(result));

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      int64_t number = arguments[0].geti64();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG(string("getBlockHash ") + to_string(number));

      evm_uint256be blockhash;
      context->fn_table->get_block_hash(&blockhash, context, number);
      storeUint256(blockhash, resultOffset);

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

    if (import->base == Name("externalCodeCopy")) {
      cout << "externalcodecopy" << endl;

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();
      uint32_t codeOffset = arguments[2].geti32();
      uint32_t length = arguments[3].geti32();

      evm_address address = loadUint160(addressOffset);
      const uint8_t *code;
      size_t code_size = context->fn_table->get_code(&code, context, &address);

      // NOTE: code will be freed by the callee (client)

      // FIXME: optimise this so not vector needs to be created
      storeMemory(vector<uint8_t>(code, code + code_size), codeOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getExternalCodeSize")) {
      uint32_t addressOffset = arguments[0].geti32();

      HERA_DEBUG(string("getExternalCodeSize ") + to_string(addressOffset));

      evm_address address = loadUint160(addressOffset);
      size_t code_size = context->fn_table->get_code(NULL, context, &address);

      HERA_DEBUG(string("getExternalCodeSize ") + to_string(address));

      return Literal((uint32_t) code_size);
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

    if (import->base == Name("getBlockGasLimit")) {
      cout << "getblockgaslimit" << endl;

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);

      return Literal((int64_t)tx_context.block_gas_limit);
    }

    if (import->base == Name("getTxGasPrice")) {
      cout << "gettxgasprice" << endl;

      uint32_t valueOffset = arguments[0].geti32();

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint128(tx_context.tx_gas_price, valueOffset);

      return Literal();
    }

    if (import->base == Name("log")) {
      cout << "log" << endl;

      uint32_t dataOffset = arguments[0].geti32();
      uint32_t length = arguments[1].geti32();
      uint32_t numberOfTopics = arguments[2].geti32();

      evm_uint256be topics[numberOfTopics];
      for (size_t i = 0; i < numberOfTopics; ++i) {
        uint32_t topicOffset = arguments[3 + i].geti32();
        topics[i] = loadUint256(topicOffset);
      }

      vector<uint8_t> data(length);
      loadMemory(dataOffset, data, length);

      context->fn_table->log(context, &msg.address, data.data(), length, topics, numberOfTopics);

      return Literal();
    }

    if (import->base == Name("getBlockNumber")) {
      cout << "getblocknumber" << endl;

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);

      return Literal(tx_context.block_number);
    }

    if (import->base == Name("getBlockTimestamp")) {
      cout << "getblocktimestamp" << endl;

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);

      return Literal(tx_context.block_timestamp);
    }

    if (import->base == Name("getTxOrigin")) {
      cout << "gettxorigin" << endl;

      uint32_t resultOffset = arguments[0].geti32();

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint160(tx_context.tx_origin, resultOffset);

      return Literal();
    }

    if (import->base == Name("storageStore")) {
      cout << "storagestore" << endl;

      uint32_t pathOffset = arguments[0].geti32();
      uint32_t valueOffset = arguments[1].geti32();

      evm_uint256be path = loadUint256(pathOffset);
      evm_uint256be value = loadUint256(valueOffset);

      context->fn_table->set_storage(context, &msg.address, &path, &value);

      return Literal();
    }

    if (import->base == Name("storageLoad")) {
      cout << "storageload" << endl;

      uint32_t pathOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      evm_uint256be path = loadUint256(pathOffset);

      evm_uint256be result;
      context->fn_table->get_storage(&result, context, &msg.address, &path);

      storeUint256(result, resultOffset);

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

      result.isRevert = import->base == Name("revert");

      return Literal();
    }

    if (import->base == Name("selfDestruct")) {
      cout << "selfdestruct" << endl;

      uint32_t addressOffset = arguments[0].geti32();

      evm_address address = loadUint160(addressOffset);
      context->fn_table->selfdestruct(context, &msg.address, &address);

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
    heraAssert(memory.size() >= (dstOffset + length), "Out of bounds (destination) memory copy.");

    for (uint32_t i = 0; i < length; ++i) {
      memory.set<uint8_t>(dstOffset + length - (i + 1), src[i]);
    }
  }

  void EthereumInterface::storeMemory(vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length)
  {
    heraAssert(length > 0, "Length must be nonzero");
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");
    heraAssert(src.size() >= (srcOffset + length), "Out of bounds (source) memory copy.");
    heraAssert((dstOffset + length) > dstOffset, "Out of bounds (destination) memory copy.");
    heraAssert(memory.size() >= (dstOffset + length), "Out of bounds (destination) memory copy.");

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
