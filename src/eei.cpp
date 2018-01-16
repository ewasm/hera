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

#define HERA_DEBUG cout

#else

struct NullStream {
  template<typename T> NullStream& operator<<(const T&) { return *this; }
};

#define HERA_DEBUG NullStream()

#endif

namespace HeraVM {
#if HERA_DEBUGGING
  Literal EthereumInterface::callDebugImport(Import *import, LiteralList& arguments) {
    heraAssert(import->module == Name("debug"), "Import namespace error.");

    if (import->base == Name("print32")) {
      uint32_t value = arguments[0].geti32();

      cout << "DEBUG print32: " << value << " " << hex << "0x" << value << dec << endl;

      return Literal();
    }

    if (import->base == Name("print64")) {
      uint64_t value = arguments[0].geti64();

      cout << "DEBUG print64: " << value << " " << hex << "0x" << value << dec << endl;

      return Literal();
    }

    if (import->base == Name("printMem") || import->base == Name("printMemHex")) {
      uint32_t offset = arguments[0].geti32();
      uint32_t length = arguments[1].geti32();

      heraAssert((offset + length) > offset, "Overflow.");
      heraAssert(memory.size() >= (offset + length), "Out of memory bounds.");

      bool useHex = import->base == Name("printMemHex");

      cout << "DEBUG printMem" << (useHex ? "Hex(" : "(") << hex << "0x" << offset << ":0x" << length << "): " << dec;
      if (useHex)
      {
        cout << hex;
        for (uint32_t i = offset; i < (offset + length); i++) {
          cout << static_cast<int>(memory.get<uint8_t>(i)) << " ";
        }
        cout << dec;
      }
      else
      {
        for (uint32_t i = offset; i < (offset + length); i++) {
          cout << memory.get<uint8_t>(i) << " ";
        }
      }
      cout << endl;

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str);
  }
#endif

  Literal EthereumInterface::callImport(Import *import, LiteralList& arguments) {
#if HERA_DEBUGGING
    if (import->module == Name("debug"))
      // Reroute to debug namespace
      return callDebugImport(import, arguments);
#endif

    heraAssert(import->module == Name("ethereum"), "Only imports from the 'ethereum' namespace are allowed.");

    if (import->base == Name("useGas")) {
      uint64_t gas = arguments[0].geti64();

      HERA_DEBUG << "useGas " << gas << "\n";

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getGasLeft")) {
      HERA_DEBUG << "getGasLeft\n";

      static_assert(is_same<decltype(result.gasLeft), uint64_t>::value, "uint64_t type expected");
      return Literal(result.gasLeft);
    }

    if (import->base == Name("getAddress")) {
      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getAddress " << hex << resultOffset << dec << "\n";

      storeUint160(msg.address, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG << "getBalance " << hex << addressOffset << " " << resultOffset << dec << "\n";

      evm_address address = loadUint160(addressOffset);
      evm_uint256be result;
      context->fn_table->get_balance(&result, context, &address);
      storeUint128(result, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      int64_t number = arguments[0].geti64();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG << "getBlockHash " << hex << number << " " << resultOffset << dec << "\n";

      evm_uint256be blockhash;
      context->fn_table->get_block_hash(&blockhash, context, number);
      storeUint256(blockhash, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallDataSize")) {
      HERA_DEBUG << "callDataSize\n";
      return Literal(static_cast<uint32_t>(msg.input_size));
    }

    if (import->base == Name("callDataCopy")) {
      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      HERA_DEBUG << "callDataCopy " << hex << resultOffset << " " << dataOffset << " " << length << dec << "\n";

      vector<uint8_t> input(msg.input, msg.input + msg.input_size);
      storeMemory(input, dataOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getCaller")) {
      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getCaller " << hex << resultOffset << dec << "\n";

      storeUint160(msg.sender, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallValue")) {
      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getCallValue " << hex << resultOffset << dec << "\n";

      storeUint128(msg.value, resultOffset);

      return Literal();
    }

    if (import->base == Name("codeCopy")) {
      uint32_t resultOffset = arguments[0].geti32();
      uint32_t codeOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      HERA_DEBUG << "codeCopy " << hex << resultOffset << " " << codeOffset << " " << length << dec << "\n";

      storeMemory(code, codeOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getCodeSize")) {
      HERA_DEBUG << "getCodeSize\n";

      return Literal(static_cast<uint32_t>(code.size()));
    }

    if (import->base == Name("externalCodeCopy")) {
      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();
      uint32_t codeOffset = arguments[2].geti32();
      uint32_t length = arguments[3].geti32();

      HERA_DEBUG << "externalCodeCopy " << hex << addressOffset << " " << resultOffset << " " << codeOffset << " " << length << dec << "\n";

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

      HERA_DEBUG << "getExternalCodeSize " << hex << addressOffset << dec << "\n";

      evm_address address = loadUint160(addressOffset);
      size_t code_size = context->fn_table->get_code(NULL, context, &address);

      return Literal(static_cast<uint32_t>(code_size));
    }

    if (import->base == Name("getBlockCoinbase")) {
      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getBlockCoinbase " << hex << resultOffset << dec << "\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint160(tx_context.block_coinbase, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockDifficulty")) {
      uint32_t offset = arguments[0].geti32();

      HERA_DEBUG << "getBlockDifficulty " << hex << offset << dec << "\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint256(tx_context.block_difficulty, offset);

      return Literal();
    }

    if (import->base == Name("getBlockGasLimit")) {
      HERA_DEBUG << "getBlockGasLimit\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);

      static_assert(is_same<decltype(tx_context.block_gas_limit), int64_t>::value, "int64_t type expected");
      return Literal(tx_context.block_gas_limit);
    }

    if (import->base == Name("getTxGasPrice")) {
      uint32_t valueOffset = arguments[0].geti32();

      HERA_DEBUG << "getTxGasPrice " << hex << valueOffset << dec << "\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint128(tx_context.tx_gas_price, valueOffset);

      return Literal();
    }

    if (import->base == Name("log")) {
      uint32_t dataOffset = arguments[0].geti32();
      uint32_t length = arguments[1].geti32();
      uint32_t numberOfTopics = arguments[2].geti32();

      HERA_DEBUG << "log " << hex << dataOffset << " " << length << " " << numberOfTopics << dec << "\n";

      heraAssert(!(msg.flags & EVM_STATIC), "\"log\" attempted in static mode");

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
      HERA_DEBUG << "getBlockNumber\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);

      static_assert(is_same<decltype(tx_context.block_number), int64_t>::value, "int64_t type expected");
      return Literal(tx_context.block_number);
    }

    if (import->base == Name("getBlockTimestamp")) {
      HERA_DEBUG << "getBlockTimestamp\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);

      static_assert(is_same<decltype(tx_context.block_timestamp), int64_t>::value, "int64_t type expected");
      return Literal(tx_context.block_timestamp);
    }

    if (import->base == Name("getTxOrigin")) {
      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getTxOrigin " << hex << resultOffset << dec << "\n";

      evm_tx_context tx_context;
      context->fn_table->get_tx_context(&tx_context, context);
      storeUint160(tx_context.tx_origin, resultOffset);

      return Literal();
    }

    if (import->base == Name("storageStore")) {
      uint32_t pathOffset = arguments[0].geti32();
      uint32_t valueOffset = arguments[1].geti32();

      HERA_DEBUG << "storageStore " << hex << pathOffset << " " << valueOffset << dec << "\n";

      heraAssert(!(msg.flags & EVM_STATIC), "\"storageStore\" attempted in static mode");

      evm_uint256be path = loadUint256(pathOffset);
      evm_uint256be value = loadUint256(valueOffset);

      evm_uint256be current;
      context->fn_table->get_storage(&current, context, &msg.address, &path);

      // We do not need to take care about the delete case (gas refund), the client does it.
      takeGas(
        (isZeroUint256(current) && !isZeroUint256(value)) ?
        GasSchedule::storageStoreCreate :
        GasSchedule::storageStoreChange
      );

      context->fn_table->set_storage(context, &msg.address, &path, &value);

      return Literal();
    }

    if (import->base == Name("storageLoad")) {
      uint32_t pathOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG << "storageLoad " << hex << pathOffset << " " << resultOffset << dec << "\n";

      evm_uint256be path = loadUint256(pathOffset);

      evm_uint256be result;
      context->fn_table->get_storage(&result, context, &msg.address, &path);

      storeUint256(result, resultOffset);

      return Literal();
    }

    if (import->base == Name("return") || import->base == Name("revert")) {
      uint32_t offset = arguments[0].geti32();
      uint32_t size = arguments[1].geti32();

      HERA_DEBUG << (import->base == Name("revert") ? "revert " : "return ") << hex << offset << " " << size << dec << "\n";

      result.returnValue.clear();
      for (uint32_t i = offset; i < offset + size; i++) {
        result.returnValue.push_back(memory.get<uint8_t>(i));
      }

      result.isRevert = import->base == Name("revert");

      return Literal();
    }

    if (import->base == Name("getReturnDataSize")) {
      HERA_DEBUG << "getReturnDataSize\n";
      return Literal(static_cast<uint32_t>(lastReturnData.size()));
    }

    if (import->base == Name("returnDataCopy")) {
      uint32_t dataOffset = arguments[0].geti32();
      uint32_t offset = arguments[1].geti32();
      uint32_t size = arguments[2].geti32();

      HERA_DEBUG << "returnDataCopy " << hex << offset << " " << size << dec << "\n";

      storeMemory(lastReturnData, dataOffset, offset, size);

      return Literal();
    }

    if (
      import->base == Name("call") ||
      import->base == Name("callCode") ||
      import->base == Name("callDelegate") ||
      import->base == Name("callStatic")
    ) {
      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t valueOffset;
      uint32_t dataOffset;
      uint32_t dataLength;
      uint32_t resultOffset;
      uint32_t resultLength;

      heraAssert((msg.flags & ~EVM_STATIC) == 0, "Unknown flags not supported.");

      evm_message call_message;
      call_message.address = loadUint160(addressOffset);
      call_message.flags = msg.flags;
      call_message.code_hash = {};
      call_message.gas = gas;
      call_message.depth = msg.depth + 1;

      if (import->base == Name("call") || import->base == Name("callCode")) {
        valueOffset = arguments[2].geti32();
        dataOffset = arguments[3].geti32();
        dataLength = arguments[4].geti32();
        resultOffset = arguments[5].geti32();
        resultLength = arguments[6].geti32();

        call_message.sender = msg.address;
        call_message.value = loadUint128(valueOffset);
        call_message.kind = (import->base == Name("callCode")) ? EVM_CALLCODE : EVM_CALL;

        if (import->base == Name("call") && !isZeroUint256(call_message.value))
          heraAssert(!(msg.flags & EVM_STATIC), "\"call\" with value transfer attempted in static mode");

        ensureSenderBalance(call_message.value);
      } else {
        valueOffset = 0;
        dataOffset = arguments[2].geti32();
        dataLength = arguments[3].geti32();
        resultOffset = arguments[4].geti32();
        resultLength = arguments[5].geti32();

        if (import->base == Name("callDelegate")) {
          call_message.sender = msg.sender;
          call_message.value = msg.value;
          call_message.kind = EVM_DELEGATECALL;
        } else if (import->base == Name("callStatic")) {
          call_message.sender = msg.address;
          call_message.value = {};
          call_message.kind = EVM_CALL;
          call_message.flags |= EVM_STATIC;
        }
      }

      HERA_DEBUG <<
        import->base << " " << hex <<
        gas << " " <<
        addressOffset << " " <<
        valueOffset << " " <<
        dataOffset << " " <<
        dataLength << " " <<
        resultOffset << " " <<
        resultLength << dec << "\n";

      if (dataLength) {
        vector<uint8_t> input_data(dataLength);
        loadMemory(dataOffset, input_data, dataLength);
        call_message.input = input_data.data();
        call_message.input_size = dataLength;
      } else {
        call_message.input = nullptr;
        call_message.input_size = 0;
      }

      evm_result call_result;
      context->fn_table->call(&call_result, context, &call_message);

      if (call_result.output_data) {
        vector<uint8_t> result(call_result.output_data, call_result.output_data + call_result.output_size);
        result.resize(resultLength);
        storeMemory(result, 0, resultOffset, resultLength);

        lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
      } else {
        lastReturnData.clear();
      }

      if (call_result.release)
        call_result.release(&call_result);

      return Literal((call_result.status_code == EVM_SUCCESS) ? uint32_t(1) : uint32_t(0));
    }

    if (import->base == Name("create")) {
      uint32_t valueOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();
      uint32_t resultOffset = arguments[3].geti32();

      HERA_DEBUG << "create" << hex << valueOffset << " " << dataOffset << " " << resultOffset << dec << " " << length << dec << "\n";

      heraAssert(!(msg.flags & EVM_STATIC), "\"create\" attempted in static mode");

      evm_message create_message;

      create_message.address = {};
      create_message.sender = msg.address;
      create_message.value = loadUint128(valueOffset);

      ensureSenderBalance(create_message.value);

      if (length) {
        vector<uint8_t> contract_code(length);
        loadMemory(dataOffset, contract_code, length);
        create_message.input = contract_code.data();
        create_message.input_size = length;
      } else {
        create_message.input = nullptr;
        create_message.input_size = 0;
      }

      create_message.code_hash = {};
      create_message.gas = result.gasLeft - (result.gasLeft / 64);
      create_message.depth = msg.depth + 1;
      create_message.kind = EVM_CREATE;
      create_message.flags = 0;

      evm_result create_result;
      context->fn_table->call(&create_result, context, &create_message);
      storeUint160(create_result.create_address, resultOffset);

      if (create_result.output_data) {
        lastReturnData.assign(create_result.output_data, create_result.output_data + create_result.output_size);
      } else {
        lastReturnData.clear();
      }

      if (create_result.release)
        create_result.release(&create_result);

      return Literal((create_result.status_code == EVM_SUCCESS) ? uint32_t(1) : uint32_t(0));
    }

    if (import->base == Name("selfDestruct")) {
      uint32_t addressOffset = arguments[0].geti32();

      HERA_DEBUG << "selfDestruct " << hex << addressOffset << dec << "\n";

      heraAssert(!(msg.flags & EVM_STATIC), "\"selfDestruct\" attempted in static mode");

      evm_address address = loadUint160(addressOffset);

      context->fn_table->selfdestruct(context, &msg.address, &address);

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str);
  }

  void EthereumInterface::takeGas(uint64_t gas)
  {
    if (gas > result.gasLeft) {
      HERA_DEBUG << "Out of gas :(\n";
      throw OutOfGasException();
    }

    result.gasLeft -= gas;
  }

  /*
   * Memory Operations
   */

  void EthereumInterface::loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length)
  {
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");
    
    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[length - (i + 1)] = memory.get<uint8_t>(srcOffset + i);
    }
  }

  void EthereumInterface::loadMemory(uint32_t srcOffset, vector<uint8_t> & dst, size_t length)
  {
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");
    heraAssert(dst.size() >= length, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec <<"\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[i] = memory.get<uint8_t>(srcOffset + i);
    }
  }

  void EthereumInterface::storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length)
  {
    heraAssert((dstOffset + length) > dstOffset, "Out of bounds (destination) memory copy.");
    heraAssert(memory.size() >= (dstOffset + length), "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      memory.set<uint8_t>(dstOffset + length - (i + 1), src[i]);
    }
  }

  void EthereumInterface::storeMemory(vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length)
  {
    heraAssert((srcOffset + length) > srcOffset, "Out of bounds (source) memory copy.");
    heraAssert(src.size() >= (srcOffset + length), "Out of bounds (source) memory copy.");
    heraAssert((dstOffset + length) > dstOffset, "Out of bounds (destination) memory copy.");
    heraAssert(memory.size() >= (dstOffset + length), "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; i++) {
      memory.set<uint8_t>(dstOffset + i, src[srcOffset + i]);
    }
  }

  /*
   * Memory Op Wrapper Functions
   */

  evm_uint256be EthereumInterface::loadUint256(uint32_t srcOffset)
  {
    evm_uint256be dst = {};
    loadMemory(srcOffset, dst.bytes, 32);
    return dst;
  }

  void EthereumInterface::storeUint256(evm_uint256be const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 32);
  }

  evm_address EthereumInterface::loadUint160(uint32_t srcOffset)
  {
    evm_address dst = {};
    loadMemory(srcOffset, dst.bytes, 20);
    return dst;
  }

  void EthereumInterface::storeUint160(evm_address const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 20);
  }

  evm_uint256be EthereumInterface::loadUint128(uint32_t srcOffset)
  {
    evm_uint256be dst = {};
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
  void EthereumInterface::ensureSenderBalance(evm_uint256be const& value)
  {
    evm_uint256be balance;
    context->fn_table->get_balance(&balance, context, &msg.address);
    if (safeLoadUint64(balance) < safeLoadUint64(value))
      throw new OutOfGasException();
  }

  uint64_t EthereumInterface::safeLoadUint64(evm_uint256be const& value)
  {
    heraAssert(!exceedsUint64(value), "Value exceeds 64 bits.");
    uint64_t ret = 0;
    for (unsigned i = 24; i < 32; i++) {
      ret <<= 8;
      ret |= value.bytes[i];
    }
    return ret;
  }

  bool EthereumInterface::exceedsUint64(evm_uint256be const& value)
  {
    for (unsigned i = 0; i < 24; i++) {
      if (value.bytes[i])
        return true;
    }
    return false;
  }

  bool EthereumInterface::exceedsUint128(evm_uint256be const& value)
  {
    for (unsigned i = 0; i < 16; i++) {
      if (value.bytes[i])
        return true;
    }
    return false;
  }

  bool EthereumInterface::isZeroUint256(evm_uint256be const& value)
  {
    for (unsigned i = 0; i < 32; i++) {
      if (value.bytes[i] != 0)
        return true;
    }
    return false;
  }
}
