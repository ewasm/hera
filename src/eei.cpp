/*
 * Copyright 2016-2018 Alex Beregszaszi et al.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdexcept>
#include <array>
#include "eei.h"
#include "exceptions.h"

#include <evmc/instructions.h>

using namespace std;
using namespace wasm;

#if HERA_DEBUGGING

#define HERA_DEBUG cerr

namespace {
string toHex(evmc_uint256be const& value) {
  ostringstream os;
  os << hex;
  for (auto b: value.bytes)
    os << setw(2) << setfill('0') << unsigned(b);
  return "0x" + os.str();
}
}

#else

struct NullStream {
  template<typename T> NullStream& operator<<(const T&) { return *this; }
};

#define HERA_DEBUG NullStream()

#endif

namespace hera {
  void EthereumInterface::importGlobals(std::map<Name, Literal>& globals, Module& wasm) {
    (void)globals;
    (void)wasm;
    HERA_DEBUG << "importGlobals\n";
  }

#if HERA_DEBUGGING
  Literal EthereumInterface::callDebugImport(Import *import, LiteralList& arguments) {
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
      heraAssert(memorySize() >= (offset + length), "Out of memory bounds.");

      bool useHex = import->base == Name("printMemHex");

      cerr << "DEBUG printMem" << (useHex ? "Hex(" : "(") << hex << "0x" << offset << ":0x" << length << "): " << dec;
      if (useHex)
      {
        cerr << hex;
        for (uint32_t i = offset; i < (offset + length); i++) {
          cerr << static_cast<int>(memoryGet(i)) << " ";
        }
        cerr << dec;
      }
      else
      {
        for (uint32_t i = offset; i < (offset + length); i++) {
          cerr << memoryGet(i) << " ";
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
      m_context->fn_table->get_storage(&result, m_context, &m_msg.destination, &path);

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

      const char* const* const opNamesTable = evmc_get_instruction_names_table(EVMC_BYZANTIUM);
      const char* opName = opNamesTable[static_cast<uint8_t>(opcode)];
      if (opName == nullptr)
        opName = "UNDEFINED";

      cout << "{\"depth\":" << dec << m_msg.depth
        << ",\"gas\":" << m_result.gasLeft
        << ",\"gasCost\":" << cost
        << ",\"op\":" << opName
        << ",\"pc\":" << pc
        << ",\"stack\":[";

      for (int32_t i = 0; i <= sp; i += stackItemSize) {
        evmc_uint256be x = loadUint256(static_cast<uint32_t>(i));
        cout << '"' << toHex(x) << '"';
        if (i != sp)
          cout << ',';
      }
      cout << "]}" << endl;

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + " arguments)");
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
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint64_t gas = arguments[0].geti64();

      HERA_DEBUG << "useGas " << gas << "\n";

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getGasLeft")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "getGasLeft\n";

      static_assert(is_same<decltype(m_result.gasLeft), uint64_t>::value, "uint64_t type expected");

      takeInterfaceGas(GasSchedule::base);

      return Literal(m_result.gasLeft);
    }

    if (import->base == Name("getAddress")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getAddress " << hex << resultOffset << dec << "\n";

      storeUint160(m_msg.destination, resultOffset);

      takeInterfaceGas(GasSchedule::base);

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG << "getBalance " << hex << addressOffset << " " << resultOffset << dec << "\n";

      evmc_address address = loadUint160(addressOffset);
      evmc_uint256be result;

      takeInterfaceGas(GasSchedule::balance);
      m_context->fn_table->get_balance(&result, m_context, &address);
      storeUint128(result, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      int64_t number = arguments[0].geti64();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG << "getBlockHash " << hex << number << " " << resultOffset << dec << "\n";

      evmc_uint256be blockhash;

      takeInterfaceGas(GasSchedule::blockhash);
      m_context->fn_table->get_block_hash(&blockhash, m_context, number);

      if (isZeroUint256(blockhash))
        return Literal(uint32_t(1));

      storeUint256(blockhash, resultOffset);

      return Literal(uint32_t(0));
    }

    if (import->base == Name("getCallDataSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "callDataSize\n";

      takeInterfaceGas(GasSchedule::base);

      return Literal(static_cast<uint32_t>(m_msg.input_size));
    }

    if (import->base == Name("callDataCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      HERA_DEBUG << "callDataCopy " << hex << resultOffset << " " << dataOffset << " " << length << dec << "\n";

      ensureCondition(ffs(GasSchedule::copy) + (ffsl(length) - 5) <= 64, OutOfGas, "Gas charge overflow");
      ensureCondition(
        numeric_limits<uint64_t>::max() - GasSchedule::verylow >= GasSchedule::copy * ((uint64_t(length) + 31) / 32),
        OutOfGas,
        "Gas charge overflow"
      );
      takeInterfaceGas(GasSchedule::verylow + GasSchedule::copy * ((uint64_t(length) + 31) / 32));

      vector<uint8_t> input(m_msg.input_data, m_msg.input_data + m_msg.input_size);
      storeMemory(input, dataOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getCaller")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getCaller " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);
      storeUint160(m_msg.sender, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallValue")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getCallValue " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);
      storeUint128(m_msg.value, resultOffset);

      return Literal();
    }

    if (import->base == Name("codeCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t codeOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      HERA_DEBUG << "codeCopy " << hex << resultOffset << " " << codeOffset << " " << length << dec << "\n";

      ensureCondition(ffs(GasSchedule::copy) + (ffsl(length) - 5) <= 64, OutOfGas, "Gas charge overflow");
      ensureCondition(
        numeric_limits<uint64_t>::max() - GasSchedule::verylow >= GasSchedule::copy * ((uint64_t(length) + 31) / 32),
        OutOfGas,
        "Gas charge overflow"
      );
      takeInterfaceGas(GasSchedule::verylow + GasSchedule::copy * ((uint64_t(length) + 31) / 32));
      storeMemory(m_code, codeOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getCodeSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "getCodeSize\n";

      takeInterfaceGas(GasSchedule::base);

      return Literal(static_cast<uint32_t>(m_code.size()));
    }

    if (import->base == Name("externalCodeCopy")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();
      uint32_t codeOffset = arguments[2].geti32();
      uint32_t length = arguments[3].geti32();

      HERA_DEBUG << "externalCodeCopy " << hex << addressOffset << " " << resultOffset << " " << codeOffset << " " << length << dec << "\n";

      ensureCondition(ffs(GasSchedule::copy) + (ffsl(length) - 5) <= 64, OutOfGas, "Gas charge overflow");
      ensureCondition(numeric_limits<uint64_t>::max() - GasSchedule::extcode >= GasSchedule::copy * ((uint64_t(length) + 31) / 32), OutOfGas, "Gas charge overflow");
      takeInterfaceGas(GasSchedule::extcode + GasSchedule::copy * ((uint64_t(length) + 31) / 32));

      evmc_address address = loadUint160(addressOffset);
      // FIXME: optimise this so no vector needs to be created
      vector<uint8_t> codeBuffer(length);
      size_t numCopied = m_context->fn_table->copy_code(m_context, &address, codeOffset, codeBuffer.data(), codeBuffer.size());
      ensureCondition(numCopied == length, InvalidMemoryAccess, "Out of bounds (source) memory copy");

      storeMemory(codeBuffer, 0, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("getExternalCodeSize")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();

      HERA_DEBUG << "getExternalCodeSize " << hex << addressOffset << dec << "\n";

      evmc_address address = loadUint160(addressOffset);
      takeInterfaceGas(GasSchedule::extcode);
      size_t code_size = m_context->fn_table->get_code_size(m_context, &address);

      return Literal(static_cast<uint32_t>(code_size));
    }

    if (import->base == Name("getBlockCoinbase")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getBlockCoinbase " << hex << resultOffset << dec << "\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);
      storeUint160(tx_context.block_coinbase, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockDifficulty")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = arguments[0].geti32();

      HERA_DEBUG << "getBlockDifficulty " << hex << offset << dec << "\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);
      storeUint256(tx_context.block_difficulty, offset);

      return Literal();
    }

    if (import->base == Name("getBlockGasLimit")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "getBlockGasLimit\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);

      static_assert(is_same<decltype(tx_context.block_gas_limit), int64_t>::value, "int64_t type expected");

      return Literal(tx_context.block_gas_limit);
    }

    if (import->base == Name("getTxGasPrice")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t valueOffset = arguments[0].geti32();

      HERA_DEBUG << "getTxGasPrice " << hex << valueOffset << dec << "\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);
      storeUint128(tx_context.tx_gas_price, valueOffset);

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

      HERA_DEBUG << "log " << hex << dataOffset << " " << length << " " << numberOfTopics << dec << "\n";

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "log");

      ensureCondition(numberOfTopics <= 4, ContractValidationFailure, "Too many topics specified");

      // FIXME: should this assert that unused topic offsets must be 0?
      array<evmc_uint256be, 4> topics;
      topics[0] = (numberOfTopics >= 1) ? loadUint256(topic1) : evmc_uint256be{};
      topics[1] = (numberOfTopics >= 2) ? loadUint256(topic2) : evmc_uint256be{};
      topics[2] = (numberOfTopics >= 3) ? loadUint256(topic3) : evmc_uint256be{};
      topics[3] = (numberOfTopics == 4) ? loadUint256(topic4) : evmc_uint256be{};

      ensureSourceMemoryBounds(dataOffset, length);
      vector<uint8_t> data(length);
      loadMemory(dataOffset, data, length);

      ensureCondition(ffsl(length) + ffs(GasSchedule::logData) <= 64, OutOfGas, "Gas charge overflow");
      ensureCondition(
        numeric_limits<uint64_t>::max() - (GasSchedule::log + GasSchedule::logTopic * numberOfTopics) >= static_cast<uint64_t>(length) * GasSchedule::logData,
        OutOfGas,
        "Gas charge overflow"
      );
      takeInterfaceGas(GasSchedule::log + (length * GasSchedule::logData) + (GasSchedule::logTopic * numberOfTopics));
      m_context->fn_table->emit_log(m_context, &m_msg.destination, data.data(), length, topics.data(), numberOfTopics);

      return Literal();
    }

    if (import->base == Name("getBlockNumber")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "getBlockNumber\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);

      static_assert(is_same<decltype(tx_context.block_number), int64_t>::value, "int64_t type expected");

      return Literal(tx_context.block_number);
    }

    if (import->base == Name("getBlockTimestamp")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "getBlockTimestamp\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);

      static_assert(is_same<decltype(tx_context.block_timestamp), int64_t>::value, "int64_t type expected");

      return Literal(tx_context.block_timestamp);
    }

    if (import->base == Name("getTxOrigin")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = arguments[0].geti32();

      HERA_DEBUG << "getTxOrigin " << hex << resultOffset << dec << "\n";

      evmc_tx_context tx_context;

      takeInterfaceGas(GasSchedule::base);
      m_context->fn_table->get_tx_context(&tx_context, m_context);
      storeUint160(tx_context.tx_origin, resultOffset);

      return Literal();
    }

    if (import->base == Name("storageStore")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = arguments[0].geti32();
      uint32_t valueOffset = arguments[1].geti32();

      HERA_DEBUG << "storageStore " << hex << pathOffset << " " << valueOffset << dec << "\n";

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "storageStore");

      evmc_uint256be path = loadUint256(pathOffset);
      evmc_uint256be value = loadUint256(valueOffset);
      evmc_uint256be current;

      m_context->fn_table->get_storage(&current, m_context, &m_msg.destination, &path);

      // We do not need to take care about the delete case (gas refund), the client does it.
      takeInterfaceGas(
        (isZeroUint256(current) && !isZeroUint256(value)) ?
        GasSchedule::storageStoreCreate :
        GasSchedule::storageStoreChange
      );

      m_context->fn_table->set_storage(m_context, &m_msg.destination, &path, &value);

      return Literal();
    }

    if (import->base == Name("storageLoad")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      HERA_DEBUG << "storageLoad " << hex << pathOffset << " " << resultOffset << dec << "\n";

      evmc_uint256be path = loadUint256(pathOffset);
      evmc_uint256be result;

      takeInterfaceGas(GasSchedule::storageLoad);
      m_context->fn_table->get_storage(&result, m_context, &m_msg.destination, &path);

      storeUint256(result, resultOffset);

      return Literal();
    }

    if (import->base == Name("finish") || import->base == Name("revert")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = arguments[0].geti32();
      uint32_t size = arguments[1].geti32();

      HERA_DEBUG << (import->base == Name("revert") ? "revert " : "finish ") << hex << offset << " " << size << dec << "\n";

      ensureSourceMemoryBounds(offset, size);
      m_result.returnValue = vector<uint8_t>(size);
      loadMemory(offset, m_result.returnValue, size);

      m_result.isRevert = import->base == Name("revert");

      throw EndExecution{};
    }

    if (import->base == Name("getReturnDataSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      HERA_DEBUG << "getReturnDataSize\n";

      takeInterfaceGas(GasSchedule::base);

      return Literal(static_cast<uint32_t>(m_lastReturnData.size()));
    }

    if (import->base == Name("returnDataCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t dataOffset = arguments[0].geti32();
      uint32_t offset = arguments[1].geti32();
      uint32_t size = arguments[2].geti32();

      HERA_DEBUG << "returnDataCopy " << hex << dataOffset << " " << offset << " " << size << dec << "\n";

      takeInterfaceGas(GasSchedule::verylow + GasSchedule::copy * ((size + 31) / 32));
      storeMemory(m_lastReturnData, offset, dataOffset, size);

      return Literal();
    }

    if (
      import->base == Name("call") ||
      import->base == Name("callCode") ||
      import->base == Name("callDelegate") ||
      import->base == Name("callStatic")
    ) {
      EEICallKind kind;
      if (import->base == Name("call"))
        kind = EEICallKind::Call;
      else if (import->base == Name("callCode"))
        kind = EEICallKind::CallCode;
      else if (import->base == Name("callDelegate"))
        kind = EEICallKind::CallDelegate;
      else if (import->base == Name("callStatic"))
        kind = EEICallKind::CallStatic;
      else
        heraAssert(false, "");

      if ((kind == EEICallKind::Call) || (kind == EEICallKind::CallCode)) {
        heraAssert(arguments.size() == 5, string("Argument count mismatch in: ") + import->base.str);
      } else {
        heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);
      }

      int64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t valueOffset;
      uint32_t dataOffset;
      uint32_t dataLength;

      if (kind == EEICallKind::Call || kind == EEICallKind::CallCode) {
        valueOffset = arguments[2].geti32();
        dataOffset = arguments[3].geti32();
        dataLength = arguments[4].geti32();
      } else {
        valueOffset = 0;
        dataOffset = arguments[2].geti32();
        dataLength = arguments[3].geti32();
      }

      heraAssert((m_msg.flags & ~EVMC_STATIC) == 0, "Unknown flags not supported.");

      evmc_message call_message;
      call_message.destination = loadUint160(addressOffset);
      call_message.flags = m_msg.flags;
      call_message.code_hash = {};
      call_message.depth = m_msg.depth + 1;

      switch (kind) {
      case EEICallKind::Call:
      case EEICallKind::CallCode:
        call_message.kind = (kind == EEICallKind::CallCode) ? EVMC_CALLCODE : EVMC_CALL;
        call_message.sender = m_msg.destination;
        call_message.value = loadUint128(valueOffset);

        if ((kind == EEICallKind::Call) && !isZeroUint256(call_message.value)) {
          ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "call");
        }
        break;
      case EEICallKind::CallDelegate:
        call_message.kind = EVMC_DELEGATECALL;
        call_message.sender = m_msg.sender;
        call_message.value = m_msg.value;
        break;
      case EEICallKind::CallStatic:
        call_message.kind = EVMC_CALL;
        call_message.flags |= EVMC_STATIC;
        call_message.sender = m_msg.destination;
        call_message.value = {};
        break;
      }

      HERA_DEBUG <<
        import->base << " " << hex <<
        gas << " " <<
        addressOffset << " " <<
        valueOffset << " " <<
        dataOffset << " " <<
        dataLength << dec << "\n";

      // NOTE: this must be declared outside the condition to ensure the memory doesn't go out of scope
      vector<uint8_t> input_data;
      if (dataLength) {
        ensureSourceMemoryBounds(dataOffset, dataLength);
        input_data.resize(dataLength);
        loadMemory(dataOffset, input_data, dataLength);
        call_message.input_data = input_data.data();
        call_message.input_size = dataLength;
      } else {
        call_message.input_data = nullptr;
        call_message.input_size = 0;
      }

      evmc_result call_result;

      // Start with base call gas
      int64_t extra_gas = GasSchedule::call;

      // Charge valuetransfer gas if value is being transferred.
      // Only charge callNewAccount gas if the account is new and value is being transferred per EIP161.
      if (!isZeroUint256(call_message.value)) {
        extra_gas += GasSchedule::valuetransfer;
        if ((kind == EEICallKind::Call) && !m_context->fn_table->account_exists(m_context, &call_message.destination))
          extra_gas += GasSchedule::callNewAccount;
      }

      // This check is in EIP150 but not in the YellowPaper
      takeInterfaceGas(extra_gas);

      // This is the gas we are forwarding to the callee.
      // Retain one 64th of it as per EIP150
      gas = std::min(gas, maxCallGas(m_result.gasLeft));

      takeInterfaceGas(gas);

      // Add gas stipend for value transfers
      if (!isZeroUint256(call_message.value))
        gas += GasSchedule::valueStipend;

      call_message.gas = gas;

      if ((kind == EEICallKind::Call) || (kind == EEICallKind::CallCode)) {
        if ((m_msg.depth >= 1024) || !enoughSenderBalanceFor(call_message.value)) {
          // Refund the deducted gas to be forwarded as it hasn't been used.
          m_result.gasLeft += call_message.gas;
          return Literal(uint32_t(1));
        }
      }

      m_context->fn_table->call(&call_result, m_context, &call_message);

      if (call_result.output_data) {
        m_lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
      } else {
        m_lastReturnData.clear();
      }

      if (call_result.release)
        call_result.release(&call_result);

      /* Return unspent gas */
      m_result.gasLeft += call_result.gas_left;

      switch (call_result.status_code) {
      case EVMC_SUCCESS:
        return Literal(uint32_t(0));
      case EVMC_REVERT:
        return Literal(uint32_t(2));
      default:
        return Literal(uint32_t(1));
      }
    }

    if (import->base == Name("create")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t valueOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();
      uint32_t resultOffset = arguments[3].geti32();

      HERA_DEBUG << "create " << hex << valueOffset << " " << dataOffset << " " << length << dec << " " << resultOffset << dec << "\n";

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "create");

      evmc_message create_message;

      create_message.destination = {};
      create_message.sender = m_msg.destination;
      create_message.value = loadUint128(valueOffset);

      if ((m_msg.depth >= 1024) || !enoughSenderBalanceFor(create_message.value))
        return Literal(uint32_t(1));

      // NOTE: this must be declared outside the condition to ensure the memory doesn't go out of scope
      vector<uint8_t> contract_code;
      if (length) {
        ensureSourceMemoryBounds(dataOffset, length);
        contract_code.resize(length);
        loadMemory(dataOffset, contract_code, length);
        create_message.input_data = contract_code.data();
        create_message.input_size = length;
      } else {
        create_message.input_data = nullptr;
        create_message.input_size = 0;
      }

      create_message.code_hash = {};
      create_message.depth = m_msg.depth + 1;
      create_message.kind = EVMC_CREATE;
      create_message.flags = 0;

      evmc_result create_result;

      takeInterfaceGas(GasSchedule::create);

      create_message.gas = maxCallGas(m_result.gasLeft);
      takeInterfaceGas(create_message.gas);

      m_context->fn_table->call(&create_result, m_context, &create_message);

      /* Return unspent gas */
      m_result.gasLeft += create_result.gas_left;

      if (create_result.status_code == EVMC_SUCCESS) {
        storeUint160(create_result.create_address, resultOffset);
        m_lastReturnData.clear();
      } else if (create_result.output_data) {
        m_lastReturnData.assign(create_result.output_data, create_result.output_data + create_result.output_size);
      } else {
        m_lastReturnData.clear();
      }

      if (create_result.release)
        create_result.release(&create_result);

      switch (create_result.status_code) {
      case EVMC_SUCCESS:
        return Literal(uint32_t(0));
      case EVMC_REVERT:
        return Literal(uint32_t(2));
      default:
        return Literal(uint32_t(1));
      }
    }

    if (import->base == Name("selfDestruct")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = arguments[0].geti32();

      HERA_DEBUG << "selfDestruct " << hex << addressOffset << dec << "\n";

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "selfDestruct");

      evmc_address address = loadUint160(addressOffset);

      if (!m_context->fn_table->account_exists(m_context, &address))
        takeInterfaceGas(GasSchedule::callNewAccount);
      takeInterfaceGas(GasSchedule::selfdestruct);
      m_context->fn_table->selfdestruct(m_context, &m_msg.destination, &address);

      throw EndExecution{};
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + " arguments)");
  }

  void EthereumInterface::takeGas(uint64_t gas)
  {
    ensureCondition(gas <= m_result.gasLeft, OutOfGas, "Out of gas.");
    m_result.gasLeft -= gas;
  }

  void EthereumInterface::takeInterfaceGas(uint64_t gas)
  {
    if (!m_meterGas)
      return;
    takeGas(gas);
  }

  /*
   * Memory Operations
   */

  void EthereumInterface::ensureSourceMemoryBounds(uint32_t offset, uint32_t length) {
    ensureCondition((offset + length) >= offset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(memorySize() >= (offset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");
  }

  void EthereumInterface::loadMemoryReverse(uint32_t srcOffset, uint8_t *dst, size_t length)
  {
    // FIXME: the source bound check is not needed as the caller already ensures it
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(memorySize() >= (srcOffset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[length - (i + 1)] = memoryGet(srcOffset + i);
    }
  }

  void EthereumInterface::loadMemory(uint32_t srcOffset, vector<uint8_t> & dst, size_t length)
  {
    // FIXME: the source bound check is not needed as the caller already ensures it
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(memorySize() >= (srcOffset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(dst.size() >= length, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec <<"\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[i] = memoryGet(srcOffset + i);
    }
  }

  void EthereumInterface::storeMemoryReverse(const uint8_t *src, uint32_t dstOffset, uint32_t length)
  {
    ensureCondition((dstOffset + length) >= dstOffset, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");
    ensureCondition(memorySize() >= (dstOffset + length), InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      memorySet(dstOffset + length - (i + 1), src[i]);
    }
  }

  void EthereumInterface::storeMemory(vector<uint8_t> const& src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length)
  {
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(src.size() >= (srcOffset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition((dstOffset + length) >= dstOffset, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");
    ensureCondition(memorySize() >= (dstOffset + length), InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; i++) {
      memorySet(dstOffset + i, src[srcOffset + i]);
    }
  }

  /*
   * Memory Op Wrapper Functions
   */

  evmc_uint256be EthereumInterface::loadUint256(uint32_t srcOffset)
  {
    evmc_uint256be dst = {};
    loadMemoryReverse(srcOffset, dst.bytes, 32);
    return dst;
  }

  void EthereumInterface::storeUint256(evmc_uint256be const& src, uint32_t dstOffset)
  {
    storeMemoryReverse(src.bytes, dstOffset, 32);
  }

  evmc_address EthereumInterface::loadUint160(uint32_t srcOffset)
  {
    evmc_address dst = {};
    loadMemoryReverse(srcOffset, dst.bytes, 20);
    return dst;
  }

  void EthereumInterface::storeUint160(evmc_address const& src, uint32_t dstOffset)
  {
    storeMemoryReverse(src.bytes, dstOffset, 20);
  }

  evmc_uint256be EthereumInterface::loadUint128(uint32_t srcOffset)
  {
    evmc_uint256be dst = {};
    loadMemoryReverse(srcOffset, dst.bytes + 16, 16);
    return dst;
  }

  void EthereumInterface::storeUint128(evmc_uint256be const& src, uint32_t dstOffset)
  {
    heraAssert(!exceedsUint128(src), "Account balance (or transaction value) exceeds 128 bits.");
    storeMemoryReverse(src.bytes + 16, dstOffset, 16);
  }

  /*
   * Utilities
   */
  bool EthereumInterface::enoughSenderBalanceFor(evmc_uint256be const& value) const
  {
    evmc_uint256be balance;
    m_context->fn_table->get_balance(&balance, m_context, &m_msg.destination);
    return safeLoadUint128(balance) >= safeLoadUint128(value);
  }

  unsigned __int128 EthereumInterface::safeLoadUint128(evmc_uint256be const& value)
  {
    heraAssert(!exceedsUint128(value), "Account balance (or transaction value) exceeds 128 bits.");
    unsigned __int128 ret = 0;
    for (unsigned i = 16; i < 32; i++) {
      ret <<= 8;
      ret |= value.bytes[i];
    }
    return ret;
  }

  bool EthereumInterface::exceedsUint128(evmc_uint256be const& value)
  {
    for (unsigned i = 0; i < 16; i++) {
      if (value.bytes[i])
        return true;
    }
    return false;
  }

  bool EthereumInterface::isZeroUint256(evmc_uint256be const& value)
  {
    for (unsigned i = 0; i < 32; i++) {
      if (value.bytes[i] != 0)
        return false;
    }
    return true;
  }
}
