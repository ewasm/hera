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
#include <array>
#include "eei.h"
#include "exceptions.h"
#include "evm-instructions.h"

using namespace std;
using namespace wasm;

#if HERA_DEBUGGING

#define HERA_DEBUG cerr

#else

struct NullStream {
  template<typename T> NullStream& operator<<(const T&) { return *this; }
};

#define HERA_DEBUG NullStream()

#endif

namespace HeraVM {

namespace {

#if HERA_DEBUGGING
string toHex(evmc_uint256be const& value) {
  ostringstream os;
  os << hex;
  for (auto b: value.bytes)
    os << setw(2) << setfill('0') << unsigned(b);
  return "0x" + os.str();
}
#endif

}

/*
 * Abstract EEI methods called by corresponding WASM import.
 */
  void EEI::eth_useGas(uint64_t gas) 
  {
    if (!meterGas)
      return;
    
    ensureCondition(gas <= result.gasLeft, OutOfGasException, "Out of gas.");

    result.gasLeft -= gas;
  }
  
  uint64_t EEI::eth_getGasLeft()
  {
    HERA_DEBUG << "getGasLeft\n";

    static_assert(is_same<decltype(result.gasLeft), uint64_t>::value, "uint64_t type expected");

    eth_useGas(GasSchedule::base);
    return result.gasLeft;
  }

  void EEI::eth_getAddress(uint32_t resultOffset)
  {
    HERA_DEBUG << "getAddress " << hex << resultOffset << dec << "\n";

    eth_useGas(GasSchedule::base);
    storeUint160(msg.destination, resultOffset);
  }

  void EEI::eth_getBalance(uint32_t addressOffset, uint32_t resultOffset)
  {
    HERA_DEBUG << "getBalance " << hex << addressOffset << " " << resultOffset << dec << "\n";

    evmc_address address = loadUint160(addressOffset);
    evmc_uint256be result;

    eth_useGas(GasSchedule::balance);
    context->fn_table->get_balance(&result, context, &address);
    storeUint128(result, resultOffset);
  }

  void EEI::eth_getBlockHash(int64_t number, uint32_t resultOffset)
  {
    HERA_DEBUG << "getBlockHash " << number << " " << hex << resultOffset << dec << "\n";

    evmc_uint256be blockhash;

    eth_useGas(GasSchedule::blockhash);
    context->fn_table->get_block_hash(&blockhash, context, number);

    storeUint256(blockhash, resultOffset);
  }

  uint32_t EEI::eth_getCallDataSize()
  {
    HERA_DEBUG << "getCallDataSize\n";

    eth_useGas(GasSchedule::base);

    return static_cast<uint32_t>(msg.input_size);
  }

  void EEI::eth_callDataCopy(uint32_t resultOffset, uint32_t dataOffset, uint32_t length)
  {
    HERA_DEBUG << "callDataCopy" << hex << resultOffset << " " << dataOffset << dec << length << "\n";
    
    ensureCondition(ffs(GasSchedule::copy) + (ffs(length) - 5) <= 64, OutOfGasException, "Gas charge overflow");
    ensureCondition(
      numeric_limits<uint64_t>::max() - GasSchedule::verylow >= GasSchedule::copy * ((uint64_t(length) + 31) / 32),
      OutOfGasException,
      "Gas charge overflow"
    );
    eth_useGas(GasSchedule::verylow + GasSchedule::copy * ((uint64_t(length) + 31) / 32));

    vector<uint8_t> input(msg.input_data, msg.input_data + msg.input_size);
    storeMemory(input, dataOffset, resultOffset, length);
  }

  void EEI::eth_getCaller(uint32_t resultOffset)
  {
    HERA_DEBUG << "getCaller " << hex << resultOffset << dec << "\n";

    eth_useGas(GasSchedule::base);

    storeUint160(msg.sender, resultOffset);
  }

  void EEI::eth_getCallValue(uint32_t resultOffset)
  {
    HERA_DEBUG << "getCallValue " << hex << resultOffset << dec << "\n";

    eth_useGas(GasSchedule::base);

    storeUint128(msg.value, resultOffset);
  }

  void EEI::eth_codeCopy(uint32_t resultOffset, uint32_t codeOffset, uint32_t length)
  {
    HERA_DEBUG << "codeCopy " << hex << resultOffset << " " << codeOffset << " " << length << dec << "\n";

    ensureCondition(ffs(GasSchedule::copy) + (ffs(length) - 5) <= 64, OutOfGasException, "Gas charge overflow");
    ensureCondition(
      numeric_limits<uint64_t>::max() - GasSchedule::verylow >= GasSchedule::copy * ((uint64_t(length) + 31) / 32),
      OutOfGasException,
      "Gas charge overflow"
    );
    eth_useGas(GasSchedule::verylow + GasSchedule::copy * ((uint64_t(length) + 31) / 32));
    storeMemory(code, codeOffset, resultOffset, length);
  }

  uint32_t EEI::eth_getCodeSize()
  {
    HERA_DEBUG << "getCodeSize\n";
    
    eth_useGas(GasSchedule::base);

    return static_cast<uint32_t>(code.size());
  }

  void EEI::eth_externalCodeCopy(uint32_t addressOffset, uint32_t resultOffset, uint32_t codeOffset, uint32_t length)
  {
    HERA_DEBUG << "externalCodeCopy " << hex << addressOffset << " " << resultOffset << " " << codeOffset << " " << length << dec << "\n";

    ensureCondition(ffs(GasSchedule::copy) + (ffs(length) - 5) <= 64, OutOfGasException, "Gas charge overflow");
    ensureCondition(numeric_limits<uint64_t>::max() - GasSchedule::extcode >= GasSchedule::copy * ((uint64_t(length) + 31) / 32), OutOfGasException, "Gas charge overflow");
    eth_useGas(GasSchedule::extcode + GasSchedule::copy * ((uint64_t(length) + 31) / 32));

    evmc_address address = loadUint160(addressOffset);
    // FIXME: optimise this so not vector needs to be created
    vector<uint8_t> codeBuffer(length);
    size_t numCopied = context->fn_table->copy_code(context, &address, codeOffset, codeBuffer.data(), codeBuffer.size());
    fill_n(&codeBuffer[numCopied], length - numCopied, 0);

    storeMemory(codeBuffer, codeOffset, resultOffset, length);
  }

  uint32_t EEI::eth_getExternalCodeSize(uint32_t addressOffset)
  {
    HERA_DEBUG << "getExternalCodeSize " << hex << addressOffset << dec << "\n";

    evmc_address address = loadUint160(addressOffset);
    eth_useGas(GasSchedule::extcode);
    return static_cast<uint32_t>(context->fn_table->get_code_size(context, &address));
  }

  void EEI::eth_getBlockCoinbase(uint32_t resultOffset)
  {
    HERA_DEBUG << "getBlockCoinbase " << hex << resultOffset << dec << "\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);
    storeUint160(tx_context.block_coinbase, resultOffset);
  }

  void EEI::eth_getBlockDifficulty(uint32_t offset)
  {
    HERA_DEBUG << "getBlockDifficulty " << hex << offset << dec << "\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);
    storeUint256(tx_context.block_difficulty, offset);
  }

  int64_t EEI::eth_getBlockGasLimit()
  {
    HERA_DEBUG << "getBlockGasLimit\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);

    static_assert(is_same<decltype(tx_context.block_gas_limit), int64_t>::value, "int64_t type expected");
    
    return tx_context.block_gas_limit;
  }

  void EEI::eth_getTxGasPrice(uint32_t valueOffset)
  {
    HERA_DEBUG << "getTxGasPrice " << hex << valueOffset << dec << "\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);
    storeUint128(tx_context.tx_gas_price, valueOffset);
  }

  void EEI::eth_log(
    uint32_t dataOffset,
    uint32_t length,
    uint32_t numberOfTopics,
    uint32_t topic1,
    uint32_t topic2,
    uint32_t topic3,
    uint32_t topic4)
  {
    HERA_DEBUG << "log " << hex << dataOffset << " " << length << " " << numberOfTopics << dec << "\n";

    ensureCondition(!(msg.flags & EVMC_STATIC), StaticModeViolation, "log");

    ensureCondition(numberOfTopics <= 4, ContractValidationFailure, "Too many topics specified");

    array<evmc_uint256be, 4> topics;

    topics[0] = (numberOfTopics >= 1) ? loadUint256(topic1) : evmc_uint256be{};
    topics[1] = (numberOfTopics >= 2) ? loadUint256(topic2) : evmc_uint256be{};
    topics[2] = (numberOfTopics >= 3) ? loadUint256(topic3) : evmc_uint256be{};
    topics[3] = (numberOfTopics == 4) ? loadUint256(topic4) : evmc_uint256be{};

    vector<uint8_t> data(length);
    loadMemory(dataOffset, data, length);

    ensureCondition(ffs(length) + ffs(GasSchedule::logData) <= 64, OutOfGasException, "Gas charge overflow");
    ensureCondition(
      numeric_limits<uint64_t>::max() - (GasSchedule::log + GasSchedule::logTopic * numberOfTopics) >= static_cast<uint64_t>(length) * GasSchedule::logData,
      OutOfGasException,
      "Gas charge overflow"
    );
    eth_useGas(GasSchedule::log + (length * GasSchedule::logData) + (GasSchedule::logTopic * numberOfTopics));
    context->fn_table->emit_log(context, &msg.destination, data.data(), length, topics.data(), numberOfTopics);
  }

  int64_t EEI::eth_getBlockNumber()
  {
    HERA_DEBUG << "getBlockNumber\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);

    static_assert(is_same<decltype(tx_context.block_number), int64_t>::value, "int64_t type expected");

    return tx_context.block_number;
  }

  int64_t EEI::eth_getBlockTimestamp()
  {
    HERA_DEBUG << "getBlockTimestamp\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);

    static_assert(is_same<decltype(tx_context.block_timestamp), int64_t>::value, "int64_t type expected");
    
    return tx_context.block_timestamp;
  }

  void EEI::eth_getTxOrigin(uint32_t resultOffset)
  {
    HERA_DEBUG << "getTxOrigin " << hex << resultOffset << dec << "\n";

    evmc_tx_context tx_context;

    eth_useGas(GasSchedule::base);
    context->fn_table->get_tx_context(&tx_context, context);
    storeUint160(tx_context.tx_origin, resultOffset);
  }

  void EEI::eth_storageStore(uint32_t pathOffset, uint32_t valueOffset)
  {
    HERA_DEBUG << "storageStore " << hex << pathOffset << " " << valueOffset << dec << "\n";

    ensureCondition(!(msg.flags & EVMC_STATIC), StaticModeViolation, "storageStore");

    evmc_uint256be path = loadUint256(pathOffset);
    evmc_uint256be value = loadUint256(valueOffset);
    evmc_uint256be current;

    context->fn_table->get_storage(&current, context, &msg.destination, &path);

    // We do not need to take care about the delete case (gas refund), the client does it.
    eth_useGas(
      (isZeroUint256(current) && !isZeroUint256(value)) ?
      GasSchedule::storageStoreCreate :
      GasSchedule::storageStoreChange
    );

    context->fn_table->set_storage(context, &msg.destination, &path, &value);
  }

  void EEI::eth_storageLoad(uint32_t pathOffset, uint32_t resultOffset)
  {
    HERA_DEBUG << "storageLoad " << hex << pathOffset << " " << resultOffset << dec << "\n";

    evmc_uint256be path = loadUint256(pathOffset);
    evmc_uint256be result;

    eth_useGas(GasSchedule::storageLoad);
    context->fn_table->get_storage(&result, context, &msg.destination, &path);

    storeUint256(result, resultOffset);
  }


/* Separate return and revert methods for clarity */
  void EEI::eth_return(uint32_t dataOffset, uint32_t length)
  {
    HERA_DEBUG << "return " << hex << dataOffset << " " << length << dec << "\n";
    result.isRevert = false;
    result.returnValue = vector<uint8_t>(length);
    loadMemory(dataOffset, result.returnValue, length);
  }

  void EEI::eth_revert(uint32_t dataOffset, uint32_t length)
  {
    HERA_DEBUG << "revert " << hex << dataOffset << " " << length << dec << "\n";
    result.isRevert = true;
    result.returnValue = vector<uint8_t>(length);
    loadMemory(dataOffset, result.returnValue, length);
  }

  uint32_t EEI::eth_getReturnDataSize()
  {
    HERA_DEBUG << "getReturnDataSize\n";

    eth_useGas(GasSchedule::base);
    
    return static_cast<uint32_t>(lastReturnData.size());
  }

  void EEI::eth_returnDataCopy(uint32_t resultOffset, uint32_t dataOffset, uint32_t length)
  {
    HERA_DEBUG << "returnDataCopy " << hex << resultOffset << " " << dataOffset << " " << length << dec << "\n";

    eth_useGas(GasSchedule::verylow);
    storeMemory(lastReturnData, dataOffset, resultOffset, length);
  }

  uint32_t EEI::eth_create(uint32_t valueOffset, uint32_t dataOffset, uint32_t length, uint32_t resultOffset)
  {
    HERA_DEBUG << "create " << hex << valueOffset << " " << dataOffset << " " << length << dec << " " << resultOffset << dec << "\n";

    ensureCondition(!(msg.flags & EVMC_STATIC), StaticModeViolation, "create");

    evmc_message create_message;

    create_message.destination = {};
    create_message.sender = msg.destination;
    create_message.value = loadUint128(valueOffset);

    ensureSenderBalance(create_message.value);

    if (length) {
      vector<uint8_t> contract_code(length);
      loadMemory(dataOffset, contract_code, length);
      create_message.input_data = contract_code.data();
      create_message.input_size = length;
    } else {
      create_message.input_data = nullptr;
      create_message.input_size = 0;
    }

    create_message.code_hash = {};
    create_message.gas = result.gasLeft - (result.gasLeft / 64);
    create_message.depth = msg.depth + 1;
    create_message.kind = EVMC_CREATE;
    create_message.flags = 0;

    evmc_result create_result;

    eth_useGas(create_message.gas);
    eth_useGas(GasSchedule::create);
    context->fn_table->call(&create_result, context, &create_message);

    if (create_result.status_code == EVMC_SUCCESS) {
      storeUint160(create_result.create_address, resultOffset);
      lastReturnData.clear();
    } else if (create_result.output_data) {
      lastReturnData.assign(create_result.output_data, create_result.output_data + create_result.output_size);
    } else {
      lastReturnData.clear();
    }

    if (create_result.release)
      create_result.release(&create_result);

    switch (create_result.status_code) {
      case EVMC_SUCCESS:
        return 0;
      case EVMC_REVERT:
        return 2;
      default:
        return 1;
    }
  }

  void EEI::eth_selfDestruct(uint32_t addressOffset)
  {
    HERA_DEBUG << "selfDestruct " << hex << addressOffset << dec << "\n";

    ensureCondition(!(msg.flags & EVMC_STATIC), StaticModeViolation, "selfDestruct");

    evmc_address address = loadUint160(addressOffset);

    if (!context->fn_table->account_exists(context, &address))
      eth_useGas(GasSchedule::callNewAccount);
    eth_useGas(GasSchedule::selfdestruct);
    context->fn_table->selfdestruct(context, &msg.destination, &address);   
  }
  /* 
   * yes, i know the call* methods are repetitive. 
   * this is better than mashing them together into unreadable spaghetti.
   */
  uint32_t EEI::eth_call(
    int64_t gas, 
    uint32_t addressOffset, 
    uint32_t valueOffset, 
    uint32_t dataOffset, 
    uint32_t dataLength)
  {
    heraAssert((msg.flags & ~EVMC_STATIC) == 0, "Unknown flags not supported.");

    evmc_message call_message;

    call_message.destination = loadUint160(addressOffset);
    call_message.flags = msg.flags;
    call_message.code_hash = { };
    call_message.gas = gas - (gas / 64);
    call_message.depth = msg.depth + 1;
    call_message.sender = msg.destination;
    call_message.value = loadUint128(valueOffset);
    call_message.kind = EVMC_CALL;
    
    if (!isZeroUint256(call_message.value))
      ensureCondition(!(msg.flags & EVMC_STATIC), StaticModeViolation, "call");
    ensureSenderBalance(call_message.value);

    HERA_DEBUG << "call " << hex <<
      gas << " " <<
      addressOffset << " " <<
      valueOffset << " " <<
      dataOffset << " " <<
      dataLength << dec << "\n";
    
    call_message.input_size = dataLength;
    if (dataLength > 0) {
      vector<uint8_t> input_data(dataLength);
      loadMemory(dataOffset, input_data, dataLength);
      call_message.input_data = input_data.data();
    } else {
      call_message.input_data = nullptr;
    }

    evmc_result call_result;

    if (!context->fn_table->account_exists(context, &call_message.destination))
      eth_useGas(GasSchedule::callNewAccount);
    if (!isZeroUint256(call_message.value))
      eth_useGas(GasSchedule::valuetransfer);

    eth_useGas(GasSchedule::call);
    eth_useGas(call_message.gas);

    context->fn_table->call(&call_result, context, &call_message);

    if (call_result.output_data)
      lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
    else
      lastReturnData.clear();

    if (call_result.release)
      call_result.release(&call_result);

    result.gasLeft += call_result.gas_left;

    switch (call_result.status_code) {
    case EVMC_SUCCESS:
      return 0;
    case EVMC_REVERT:
      return 2;
    default:
      return 1;
    }
  }

  uint32_t EEI::eth_callCode(
    int64_t gas,
    uint32_t addressOffset,
    uint32_t valueOffset,
    uint32_t dataOffset,
    uint32_t dataLength)
  {
    heraAssert((msg.flags & ~EVMC_STATIC) == 0, "Unknown flags not supported.");

    evmc_message call_message;

    call_message.destination = loadUint160(addressOffset);
    call_message.flags = msg.flags;
    call_message.code_hash = { };
    call_message.gas = gas - (gas / 64);
    call_message.depth = msg.depth + 1;
    call_message.sender = msg.destination;
    call_message.value = loadUint128(valueOffset);
    call_message.kind = EVMC_CALLCODE;

    ensureSenderBalance(call_message.value);

    HERA_DEBUG << "callCode " << hex <<
      gas << " " <<
      addressOffset << " " <<
      valueOffset << " " <<
      dataOffset << " " <<
      dataLength << dec << "\n";
    
    call_message.input_size = dataLength;
    if (dataLength > 0) {
      vector<uint8_t> input_data(dataLength);
      loadMemory(dataOffset, input_data, dataLength);
      call_message.input_data = input_data.data();
    } else {
      call_message.input_data = nullptr;
    }

    evmc_result call_result;

    if (!isZeroUint256(call_message.value))
      eth_useGas(GasSchedule::valuetransfer);

    eth_useGas(GasSchedule::call);
    eth_useGas(call_message.gas);

    context->fn_table->call(&call_result, context, &call_message);

    if (call_result.output_data)
      lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
    else
      lastReturnData.clear();

    if (call_result.release)
      call_result.release(&call_result);

    result.gasLeft += call_result.gas_left;

    switch (call_result.status_code) {
    case EVMC_SUCCESS:
      return 0;
    case EVMC_REVERT:
      return 2;
    default:
      return 1;
    }
  }

  uint32_t EEI::eth_callDelegate(int64_t gas, uint32_t addressOffset, uint32_t dataOffset, uint32_t dataLength)
  {
    heraAssert((msg.flags & ~EVMC_STATIC) == 0, "Unknown flags not supported.");

    evmc_message call_message;

    call_message.destination = loadUint160(addressOffset);
    call_message.flags = msg.flags;
    call_message.code_hash = { };
    call_message.gas = gas - (gas / 64);
    call_message.depth = msg.depth + 1;
    call_message.sender = msg.sender;
    call_message.value = msg.value;
    call_message.kind = EVMC_DELEGATECALL;

    HERA_DEBUG << "callDelegate " << hex <<
      gas << " " <<
      addressOffset << " " <<
      dataOffset << " " <<
      dataLength << dec << "\n";
    
    call_message.input_size = dataLength;
    if (dataLength > 0) {
      vector<uint8_t> input_data(dataLength);
      loadMemory(dataOffset, input_data, dataLength);
      call_message.input_data = input_data.data();
    } else {
      call_message.input_data = nullptr;
    }

    evmc_result call_result;

    if(!isZeroUint256(call_message.value))
      eth_useGas(GasSchedule::valuetransfer);

    eth_useGas(GasSchedule::call);
    eth_useGas(call_message.gas);

    context->fn_table->call(&call_result, context, &call_message);

    if (call_result.output_data)
      lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
    else
      lastReturnData.clear();

    if (call_result.release)
      call_result.release(&call_result);

    result.gasLeft += call_result.gas_left;

    switch (call_result.status_code) {
    case EVMC_SUCCESS:
      return 0;
    case EVMC_REVERT:
      return 2;
    default:
      return 1;
    }
  }

  uint32_t EEI::eth_callStatic(int64_t gas, uint32_t addressOffset, uint32_t dataOffset, uint32_t dataLength)
  {
    heraAssert((msg.flags & ~EVMC_STATIC) == 0, "Unknown flags not supported.");

    evmc_message call_message;

    call_message.destination = loadUint160(addressOffset);
    call_message.flags = msg.flags;
    call_message.code_hash = { };
    call_message.gas = gas - (gas / 64);
    call_message.depth = msg.depth + 1;
    call_message.sender = msg.destination;
    call_message.value = { };
    call_message.kind = EVMC_CALL;
    call_message.flags |= EVMC_STATIC;

    HERA_DEBUG << "callDelegate " << hex <<
      gas << " " <<
      addressOffset << " " <<
      dataOffset << " " <<
      dataLength << dec << "\n";

    call_message.input_size = dataLength;
    if (dataLength > 0) {
      vector<uint8_t> input_data(dataLength);
      loadMemory(dataOffset, input_data, dataLength);
      call_message.input_data = input_data.data();
    } else {
      call_message.input_data = nullptr;
    }

    evmc_result call_result;

    if(!isZeroUint256(call_message.value))
      eth_useGas(GasSchedule::valuetransfer);

    eth_useGas(GasSchedule::call);
    eth_useGas(call_message.gas);

    context->fn_table->call(&call_result, context, &call_message);

    if (call_result.output_data)
      lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
    else
      lastReturnData.clear();

    if (call_result.release)
      call_result.release(&call_result);

    result.gasLeft += call_result.gas_left;

    switch (call_result.status_code) {
    case EVMC_SUCCESS:
      return 0;
    case EVMC_REVERT:
      return 2;
    default:
      return 1;
    }
  }

  /*
   * Abstract helper functions used in the EEI
   */
  void EEI::loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length)
  {
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[length - (i + 1)] = memory_getbyte(srcOffset + i);
    }
  }

  void EEI::loadMemory(uint32_t srcOffset, vector<uint8_t> & dst, size_t length)
  {
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(dst.size() >= length, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec <<"\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[i] = memory_getbyte(srcOffset + i);
    }
  }

  void EEI::storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length)
  {
    ensureCondition((dstOffset + length) >= dstOffset, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");
    ensureCondition(memory_size() >= (dstOffset + length), InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      memory_setbyte(dstOffset + length - (i + 1), src[i]);
    }
  }

  void EEI::storeMemory(vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length)
  {
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(src.size() >= (srcOffset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition((dstOffset + length) >= dstOffset, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");
    ensureCondition(memory_size() >= (dstOffset + length), InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; i++) {
      memory_setbyte(dstOffset + i, src[srcOffset + i]);
    }
  }

  /*
   * Memory Op Wrapper Functions
   */

  evmc_uint256be EEI::loadUint256(uint32_t srcOffset)
  {
    evmc_uint256be dst = {};
    loadMemory(srcOffset, dst.bytes, 32);
    return dst;
  }

  void EEI::storeUint256(evmc_uint256be const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 32);
  }

  evmc_address EEI::loadUint160(uint32_t srcOffset)
  {
    evmc_address dst = {};
    loadMemory(srcOffset, dst.bytes, 20);
    return dst;
  }

  void EEI::storeUint160(evmc_address const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 20);
  }

  evmc_uint256be EEI::loadUint128(uint32_t srcOffset)
  {
    evmc_uint256be dst = {};
    loadMemory(srcOffset, dst.bytes + 16, 16);
    return dst;
  }

  void EEI::storeUint128(evmc_uint256be const& src, uint32_t dstOffset)
  {
    // TODO: use a specific error code here?
    ensureCondition(!exceedsUint128(src), OutOfGasException, "Value exceeds 128 bits.");
    storeMemory(src.bytes + 16, dstOffset, 16);
  }

  /*
   * Numeric checks.
   */
  void EEI::ensureSenderBalance(evmc_uint256be const& value)
  {
    evmc_uint256be balance;
    context->fn_table->get_balance(&balance, context, &msg.destination);
    ensureCondition(safeLoadUint128(balance) >= safeLoadUint128(value), OutOfGasException, "Out of gas.");
  }

  uint64_t EEI::safeLoadUint128(evmc_uint256be const& value)
  {
    // TODO: use a specific error code here?
    ensureCondition(!exceedsUint128(value), OutOfGasException, "Value exceeds 128 bits.");
    uint64_t ret = 0;
    for (unsigned i = 16; i < 32; i++) {
      ret <<= 8;
      ret |= value.bytes[i];
    }
    return ret;
  }

  bool EEI::exceedsUint64(evmc_uint256be const& value)
  {
    for (unsigned i = 0; i < 24; i++) {
      if (value.bytes[i])
        return true;
    }
    return false;
  }

  bool EEI::exceedsUint128(evmc_uint256be const& value)
  {
    for (unsigned i = 0; i < 16; i++) {
      if (value.bytes[i])
        return true;
    }
    return false;
  }

  bool EEI::isZeroUint256(evmc_uint256be const& value)
  {
    for (unsigned i = 0; i < 32; i++) {
      if (value.bytes[i] != 0)
        return false;
    }
    return true;
  }

/*
 * Binaryen-specific implementation of byte-level memory access functions.
 */
  uint8_t BinaryenEEI::memory_getbyte(uint32_t offset) { return memory.get<uint8_t>(offset); }
  void BinaryenEEI::memory_setbyte(uint32_t offset, uint8_t val) { memory.set<uint8_t>(offset, val); }
  size_t BinaryenEEI::memory_size() { return memory.size(); }

/*
 * Binaryen EEI imports.
 */
  void BinaryenEEI::importGlobals(std::map<Name, Literal>& globals, Module& wasm) {
    (void)globals;
    (void)wasm;
    HERA_DEBUG << "importGlobals\n";
  }

#if HERA_DEBUGGING
  Literal BinaryenEEI::callDebugImport(Import *import, LiteralList& arguments) {
    heraAssert(import->module == Name("debug"), "Import namespace error.");

    if (import->base == Name("print32")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t value = arguments[0].geti32();

      cerr << "DEBUG print32: " << value << " " << hex << "0x" << value << dec << endl;

      return Literal();
    }

    if (import->base == Name("print64")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint64_t value = arguments[0].geti64();

      cerr << "DEBUG print64: " << value << " " << hex << "0x" << value << dec << endl;

      return Literal();
    }

    if (import->base == Name("printMem") || import->base == Name("printMemHex")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = arguments[0].geti32();
      uint32_t length = arguments[1].geti32();

      heraAssert((offset + length) > offset, "Overflow.");
      heraAssert(memory.size() >= (offset + length), "Out of memory bounds.");

      bool useHex = import->base == Name("printMemHex");

      cerr << "DEBUG printMem" << (useHex ? "Hex(" : "(") << hex << "0x" << offset << ":0x" << length << "): " << dec;
      if (useHex)
      {
        cerr << hex;
        for (uint32_t i = offset; i < (offset + length); i++) {
          cerr << static_cast<int>(memory.get<uint8_t>(i)) << " ";
        }
        cerr << dec;
      }
      else
      {
        for (uint32_t i = offset; i < (offset + length); i++) {
          cerr << memory.get<uint8_t>(i) << " ";
        }
      }
      cerr << endl;

      return Literal();
    }

    if (import->base == Name("printStorage") || import->base == Name("printStorageHex")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = arguments[0].geti32();

      evmc_uint256be path = loadUint256(pathOffset);

      bool useHex = import->base == Name("printStorageHex");

      HERA_DEBUG << "DEBUG printStorage" << (useHex ? "Hex" : "") << "(0x" << hex;

      // Print out the path
      for (uint8_t b: path.bytes)
        cerr << static_cast<int>(b);

      HERA_DEBUG << "): " << dec;

      evmc_uint256be result;
      context->fn_table->get_storage(&result, context, &msg.destination, &path);

      if (useHex)
      {
        cerr << hex;
        for (uint8_t b: result.bytes)
          cerr << static_cast<int>(b) << " ";
        cerr << dec;
      }
      else
      {
        for (uint8_t b: result.bytes)
          cerr << b << " ";
      }
      cerr << endl;

      return Literal();
    }

    if (import->base == Name("evmTrace")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pc = static_cast<uint32_t>(arguments[0].geti32());
      int32_t opcode = arguments[1].geti32();
      uint32_t cost = static_cast<uint32_t>(arguments[2].geti32());
      int32_t sp = arguments[3].geti32();

      HERA_DEBUG << "evmTrace\n";

      static constexpr int stackItemSize = sizeof(evmc_uint256be);
      heraAssert(sp <= (1024 * stackItemSize), "EVM stack pointer out of bounds.");
      heraAssert(opcode >= 0x00 && opcode <= 0xff, "Invalid EVM instruction.");

      auto it = evmInstructionNames.find(static_cast<uint8_t>(opcode));
      string opName = (it != evmInstructionNames.end()) ? it->second : "UNKNOWN";

      cout << "{\"depth\":" << dec << msg.depth
        << ",\"gas\":" << result.gasLeft
        << ",\"gasCost\":" << cost
        << ",\"op\":" << opName
        << ",\"pc\":" << pc
        << ",\"stack\":[";

      for (int32_t i = sp; i >= 0; i -= stackItemSize) {
        if (i != sp)
          cout << ',';
        evmc_uint256be x = loadUint256(static_cast<uint32_t>(i));
        cout << '"' << toHex(x) << '"';
      }
      cout << "]}" << endl;

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + "arguments)");
  }
#endif

/*
 * Binaryen-specific function import callback.
 * In our case, the number of arguments is verified depending on the import name
 * and said arguments are passed to the correct abstract interface function.
 */
  Literal BinaryenEEI::callImport(Import *import, LiteralList& arguments) {
#if HERA_DEBUGGING
    if (import->module == Name("debug"))
      // Reroute to debug namespace
      return callDebugImport(import, arguments);
#endif

    heraAssert(import->module == Name("ethereum"), "Only imports from the 'ethereum' namespace are allowed.");

    if (import->base == Name("useGas")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint64_t gas = arguments[0].geti64();

      HERA_DEBUG << "useGas " << gas << "\n";

      eth_useGas(gas);

      return Literal();
    }

    if (import->base == Name("getGasLeft")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eth_getGasLeft());
    }

    if (import->base == Name("getAddress")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      eth_getAddress(resultOffset);

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      eth_getBalance(addressOffset, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      int64_t number = arguments[0].geti64();
      uint32_t resultOffset = arguments[1].geti32();

      eth_getBlockHash(number, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallDataSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eth_getCallDataSize());
    }

    if (import->base == Name("callDataCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      eth_callDataCopy(resultOffset, dataOffset, length);

      return Literal();
    }

    if (import->base == Name("getCaller")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      eth_getCaller(resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallValue")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      eth_getCallValue(resultOffset);

      return Literal();
    }

    if (import->base == Name("codeCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t codeOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      eth_codeCopy(resultOffset, codeOffset, length);

      return Literal();
    }

    if (import->base == Name("getCodeSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eth_getCodeSize());
    }

    if (import->base == Name("externalCodeCopy")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();
      uint32_t codeOffset = arguments[2].geti32();
      uint32_t length = arguments[3].geti32();

      eth_externalCodeCopy(addressOffset, resultOffset, codeOffset, length);

      return Literal();
    }

    if (import->base == Name("getExternalCodeSize")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();

      return Literal(eth_getExternalCodeSize(addressOffset));
    }

    if (import->base == Name("getBlockCoinbase")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      eth_getBlockCoinbase(resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockDifficulty")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = arguments[0].geti32();
      
      eth_getBlockDifficulty(offset);
      
      return Literal();
    }

    if (import->base == Name("getBlockGasLimit")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);
      
      return Literal(eth_getBlockGasLimit());
    }

    if (import->base == Name("getTxGasPrice")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t valueOffset = arguments[0].geti32();

      eth_getTxGasPrice(valueOffset);

      return Literal();
    }

    if (import->base == Name("log")) {
      heraAssert(arguments.size() == 7, string("Argument count mismatch in: ") + import->base.str);

      uint32_t dataOffset = arguments[0].geti32();
      uint32_t length = arguments[1].geti32();
      uint32_t numberOfTopics = arguments[2].geti32();
      uint32_t topic1 = arguments[3].geti32();
      uint32_t topic2 = arguments[4].geti32();
      uint32_t topic3 = arguments[5].geti32();
      uint32_t topic4 = arguments[6].geti32();

      eth_log(dataOffset, length, numberOfTopics, topic1, topic2, topic3, topic4);

      return Literal();
    }

    if (import->base == Name("getBlockNumber")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);
      
      return Literal(eth_getBlockNumber());
    }

    if (import->base == Name("getBlockTimestamp")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);
      
      return Literal(eth_getBlockTimestamp());
    }

    if (import->base == Name("getTxOrigin")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      eth_getTxOrigin(resultOffset);

      return Literal();
    }

    if (import->base == Name("storageStore")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = arguments[0].geti32();
      uint32_t valueOffset = arguments[1].geti32();
      
      eth_storageStore(pathOffset, valueOffset);
      
      return Literal();
    }

    if (import->base == Name("storageLoad")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      eth_storageLoad(pathOffset, resultOffset);

      return Literal();
    }

    if (import->base == Name("return") || import->base == Name("revert")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t dataOffset = arguments[0].geti32();
      uint32_t length = arguments[1].geti32();
      
      if (import->base == Name("revert"))
        eth_revert(dataOffset, length);
      else
        eth_return(dataOffset, length);
      
      return Literal();
    }

    if (import->base == Name("getReturnDataSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eth_getReturnDataSize());
    }

    if (import->base == Name("returnDataCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();
      
      eth_returnDataCopy(resultOffset, dataOffset, length);
      
      return Literal();
    }

    if (import->base == Name("call")) {
      heraAssert(arguments.size() == 5, string("Argument count mismatch in: ") + import->base.str);

      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t valueOffset = arguments[2].geti32();
      uint32_t dataOffset = arguments[3].geti32();
      uint32_t dataLength = arguments[4].geti32();

      return Literal(eth_call(gas, addressOffset, valueOffset, dataOffset, dataLength));
    }

    if (import->base == Name("callCode")) {
      heraAssert(arguments.size() == 5, string("Argument count mismatch in: ") + import->base.str);

      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t valueOffset = arguments[2].geti32();
      uint32_t dataOffset = arguments[3].geti32();
      uint32_t dataLength = arguments[4].geti32();

      return Literal(eth_callCode(gas, addressOffset, valueOffset, dataOffset, dataLength));
    }

    if (import->base == Name("callDelegate")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t dataOffset = arguments[2].geti32();
      uint32_t dataLength = arguments[3].geti32();

      return Literal(eth_callDelegate(gas, addressOffset, dataOffset, dataLength));
    }

    if (import->base == Name("callStatic")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t dataOffset = arguments[2].geti32();
      uint32_t dataLength = arguments[3].geti32();

      return Literal(eth_callStatic(gas, addressOffset, dataOffset, dataLength));
    }

    if (import->base == Name("create")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t valueOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();
      uint32_t resultOffset = arguments[3].geti32();

      return Literal(eth_create(valueOffset, dataOffset, length, resultOffset)); 
    }

    if (import->base == Name("selfDestruct")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();

      eth_selfDestruct(addressOffset);

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + "arguments)");
  }

}
