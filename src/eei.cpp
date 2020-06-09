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

#include <array>
#include <fstream>
#include <iostream>

#include "debugging.h"
#include "eei.h"
#include "exceptions.h"
#include "helpers.h"

#include <evmc/instructions.h>

using namespace std;

namespace hera {
namespace
{
/* Checks if host supplied 256 bit value exceeds UINT128_MAX */
bool exceedsUint128(evmc_uint256be const& value) noexcept
{
    for (unsigned i = 0; i < 16; i++)
    {
        if (value.bytes[i])
            return true;
    }
    return false;
}
}  // namespace

bool WasmEngine::benchmarkingEnabled = false;

void WasmEngine::collectBenchmarkingData()
{
  // Convert duration to string with microsecond units.
  constexpr auto to_us_str = [](clock::duration d) {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(d).count());
  };

  const auto now = clock::now();
  const auto instantiationDuration = executionStartTime - instantiationStartTime;
  const auto executionDuration = now - executionStartTime;

  const auto log = "Time [us]: " + to_us_str(instantiationDuration + executionDuration) +
                   " (instantiation: " + to_us_str(instantiationDuration) +
                   ", execution: " + to_us_str(executionDuration) + ")\n";
  std::cerr << log;
  std::ofstream{"hera_benchmarks.log", std::ios::out | std::ios::app} << log;
}

#if HERA_DEBUGGING
  void EthereumInterface::debugPrintMem(bool useHex, uint32_t offset, uint32_t length)
  {
      heraAssert((offset + length) > offset, "Overflow.");
      heraAssert(memorySize() >= (offset + length), "Out of memory bounds.");

      cerr << depthToString() << " DEBUG printMem" << (useHex ? "Hex(" : "(") << hex << "0x" << offset << ":0x" << length << "): " << dec;
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
  }

  void EthereumInterface::debugPrintStorage(bool useHex, uint32_t pathOffset)
  {
      evmc_uint256be path = loadBytes32(pathOffset);

      HERA_DEBUG << depthToString() << " DEBUG printStorage" << (useHex ? "Hex" : "") << "(0x" << hex;

      // Print out the path
      for (uint8_t b: path.bytes)
        cerr << static_cast<int>(b);

      HERA_DEBUG << "): " << dec;

      evmc_bytes32 result = m_host.get_storage(m_msg.destination, path);

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
  }

  void EthereumInterface::debugEvmTrace(uint32_t pc, int32_t opcode, uint32_t cost, int32_t sp)
  {
      HERA_DEBUG << depthToString() << " evmTrace\n";

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
  }
#endif

  void EthereumInterface::eeiUseGas(int64_t gas)
  {
      HERA_DEBUG << depthToString() << " useGas " << gas << "\n";

      ensureCondition(gas >= 0, ArgumentOutOfRange, "Negative gas supplied.");

      takeGas(gas);
  }

  int64_t EthereumInterface::eeiGetGasLeft()
  {
      HERA_DEBUG << depthToString() << " getGasLeft\n";

      static_assert(is_same<decltype(m_result.gasLeft), int64_t>::value, "int64_t type expected");

      takeInterfaceGas(GasSchedule::base);

      return m_result.gasLeft;
  }

  void EthereumInterface::eeiGetAddress(uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getAddress " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeAddress(m_msg.destination, resultOffset);
  }

  void EthereumInterface::eeiGetExternalBalance(uint32_t addressOffset, uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getExternalBalance " << hex << addressOffset << " " << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::balance);

      evmc_address address = loadAddress(addressOffset);
      evmc_uint256be balance = m_host.get_balance(address);
      storeUint128(balance, resultOffset);
  }

  uint32_t EthereumInterface::eeiGetBlockHash(uint64_t number, uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getBlockHash " << hex << number << " " << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::blockhash);

      const auto blockhash = m_host.get_block_hash(static_cast<int64_t>(number));

      if (is_zero(blockhash))
        return 1;

      storeBytes32(blockhash, resultOffset);

      return 0;
  }

  uint32_t EthereumInterface::eeiGetCallDataSize()
  {
      HERA_DEBUG << depthToString() << " getCallDataSize\n";

      takeInterfaceGas(GasSchedule::base);

      return static_cast<uint32_t>(m_msg.input_size);
  }

  void EthereumInterface::eeiCallDataCopy(uint32_t resultOffset, uint32_t dataOffset, uint32_t length)
  {
      HERA_DEBUG << depthToString() << " callDataCopy " << hex << resultOffset << " " << dataOffset << " " << length << dec << "\n";

      safeChargeDataCopy(length, GasSchedule::verylow);

      storeMemory({m_msg.input_data, m_msg.input_size}, dataOffset, resultOffset, length);
  }

  void EthereumInterface::eeiGetCaller(uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getCaller " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeAddress(m_msg.sender, resultOffset);
  }

  void EthereumInterface::eeiGetCallValue(uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getCallValue " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeUint128(m_msg.value, resultOffset);
  }

  void EthereumInterface::eeiCodeCopy(uint32_t resultOffset, uint32_t codeOffset, uint32_t length)
  {
      HERA_DEBUG << depthToString() << " codeCopy " << hex << resultOffset << " " << codeOffset << " " << length << dec << "\n";

      safeChargeDataCopy(length, GasSchedule::verylow);

      storeMemory(m_code, codeOffset, resultOffset, length);
  }

  uint32_t EthereumInterface::eeiGetCodeSize()
  {
      HERA_DEBUG << depthToString() << " getCodeSize\n";

      takeInterfaceGas(GasSchedule::base);

      return static_cast<uint32_t>(m_code.size());
  }

  void EthereumInterface::eeiExternalCodeCopy(uint32_t addressOffset, uint32_t resultOffset, uint32_t codeOffset, uint32_t length)
  {
      HERA_DEBUG << depthToString() << " externalCodeCopy " << hex << addressOffset << " " << resultOffset << " " << codeOffset << " " << length << dec << "\n";

      safeChargeDataCopy(length, GasSchedule::extcode);

      evmc_address address = loadAddress(addressOffset);
      // TODO: optimise this so no copy needs to be created
      bytes codeBuffer(length, '\0');
      size_t numCopied = m_host.copy_code(address, codeOffset, codeBuffer.data(), codeBuffer.size());
      ensureCondition(numCopied == length, InvalidMemoryAccess, "Out of bounds (source) memory copy");

      storeMemory(codeBuffer, 0, resultOffset, length);
  }

  uint32_t EthereumInterface::eeiGetExternalCodeSize(uint32_t addressOffset)
  {
      HERA_DEBUG << depthToString() << " getExternalCodeSize " << hex << addressOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::extcode);

      evmc_address address = loadAddress(addressOffset);
      size_t code_size = m_host.get_code_size(address);

      return static_cast<uint32_t>(code_size);
  }

  void EthereumInterface::eeiGetBlockCoinbase(uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getBlockCoinbase " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeAddress(m_host.get_tx_context().block_coinbase, resultOffset);
  }

  void EthereumInterface::eeiGetBlockDifficulty(uint32_t offset)
  {
      HERA_DEBUG << depthToString() << " getBlockDifficulty " << hex << offset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeUint256(m_host.get_tx_context().block_difficulty, offset);
  }

  int64_t EthereumInterface::eeiGetBlockGasLimit()
  {
      HERA_DEBUG << depthToString() << " getBlockGasLimit\n";

      takeInterfaceGas(GasSchedule::base);

      static_assert(is_same<decltype(m_host.get_tx_context().block_gas_limit), int64_t>::value, "int64_t type expected");

      return m_host.get_tx_context().block_gas_limit;
  }

  void EthereumInterface::eeiGetTxGasPrice(uint32_t valueOffset)
  {
      HERA_DEBUG << depthToString() << " getTxGasPrice " << hex << valueOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeUint128(m_host.get_tx_context().tx_gas_price, valueOffset);
  }

  void EthereumInterface::eeiLog(uint32_t dataOffset, uint32_t length, uint32_t numberOfTopics, uint32_t topic1, uint32_t topic2, uint32_t topic3, uint32_t topic4)
  {
      HERA_DEBUG << depthToString() << " log " << hex << dataOffset << " " << length << " " << numberOfTopics << dec << "\n";

      static_assert(GasSchedule::log <= 65536, "Gas cost of log could lead to overflow");
      static_assert(GasSchedule::logTopic <= 65536, "Gas cost of logTopic could lead to overflow");
      static_assert(GasSchedule::logData <= 65536, "Gas cost of logData could lead to overflow");
      // Using uint64_t to force a type issue if the underlying API changes.
      takeInterfaceGas(GasSchedule::log + (GasSchedule::logTopic * numberOfTopics) + (GasSchedule::logData * int64_t(length)));

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "log");

      ensureCondition(numberOfTopics <= 4, ContractValidationFailure, "Too many topics specified");

      // TODO: should this assert that unused topic offsets must be 0?
      array<evmc::uint256be, 4> topics;
      topics[0] = (numberOfTopics >= 1) ? loadBytes32(topic1) : evmc::uint256be{};
      topics[1] = (numberOfTopics >= 2) ? loadBytes32(topic2) : evmc::uint256be{};
      topics[2] = (numberOfTopics >= 3) ? loadBytes32(topic3) : evmc::uint256be{};
      topics[3] = (numberOfTopics == 4) ? loadBytes32(topic4) : evmc::uint256be{};

      ensureSourceMemoryBounds(dataOffset, length);
      bytes data(length, '\0');
      loadMemory(dataOffset, data, length);

      m_host.emit_log(m_msg.destination, data.data(), length, topics.data(), numberOfTopics);
  }

  int64_t EthereumInterface::eeiGetBlockNumber()
  {
      HERA_DEBUG << depthToString() << " getBlockNumber\n";

      takeInterfaceGas(GasSchedule::base);

      static_assert(is_same<decltype(m_host.get_tx_context().block_number), int64_t>::value, "int64_t type expected");

      return m_host.get_tx_context().block_number;
  }

  int64_t EthereumInterface::eeiGetBlockTimestamp()
  {
      HERA_DEBUG << depthToString() << " getBlockTimestamp\n";

      takeInterfaceGas(GasSchedule::base);

      static_assert(is_same<decltype(m_host.get_tx_context().block_timestamp), int64_t>::value, "int64_t type expected");

      return m_host.get_tx_context().block_timestamp;
  }

  void EthereumInterface::eeiGetTxOrigin(uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " getTxOrigin " << hex << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::base);

      storeAddress(m_host.get_tx_context().tx_origin, resultOffset);
  }

  void EthereumInterface::eeiStorageStore(uint32_t pathOffset, uint32_t valueOffset)
  {
      HERA_DEBUG << depthToString() << " storageStore " << hex << pathOffset << " " << valueOffset << dec << "\n";

      static_assert(
        GasSchedule::storageStoreCreate >= GasSchedule::storageStoreChange,
        "storageStoreChange costs more than storageStoreCreate"
      );

      // Charge this here as it is the minimum cost.
      takeInterfaceGas(GasSchedule::storageStoreChange);

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "storageStore");

      const auto path = loadBytes32(pathOffset);
      const auto value = loadBytes32(valueOffset);
      const auto current = m_host.get_storage(m_msg.destination, path);

      // Charge the right amount in case of the create case.
      if (is_zero(current) && !is_zero(value))
        takeInterfaceGas(GasSchedule::storageStoreCreate - GasSchedule::storageStoreChange);

      // We do not need to take care about the delete case (gas refund), the client does it.

      m_host.set_storage(m_msg.destination, path, value);
  }

  void EthereumInterface::eeiStorageLoad(uint32_t pathOffset, uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " storageLoad " << hex << pathOffset << " " << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::storageLoad);

      evmc_bytes32 path = loadBytes32(pathOffset);
      evmc_bytes32 result = m_host.get_storage(m_msg.destination, path);

      storeBytes32(result, resultOffset);
  }

  void EthereumInterface::eeiRevertOrFinish(bool revert, uint32_t offset, uint32_t size)
  {
      HERA_DEBUG << depthToString() << " " << (revert ? "revert " : "finish ") << hex << offset << " " << size << dec << "\n";

      ensureSourceMemoryBounds(offset, size);
      m_result.returnValue = bytes(size, '\0');
      loadMemory(offset, m_result.returnValue, size);

      m_result.isRevert = revert;

      throw EndExecution{};
  }

  uint32_t EthereumInterface::eeiGetReturnDataSize()
  {
      HERA_DEBUG << depthToString() << " getReturnDataSize\n";

      takeInterfaceGas(GasSchedule::base);

      return static_cast<uint32_t>(m_lastReturnData.size());
  }

  void EthereumInterface::eeiReturnDataCopy(uint32_t dataOffset, uint32_t offset, uint32_t size)
  {
      HERA_DEBUG << depthToString() << " returnDataCopy " << hex << dataOffset << " " << offset << " " << size << dec << "\n";

      safeChargeDataCopy(size, GasSchedule::verylow);

      storeMemory(m_lastReturnData, offset, dataOffset, size);
  }

  uint32_t EthereumInterface::eeiCall(EEICallKind kind, int64_t gas, uint32_t addressOffset, uint32_t valueOffset, uint32_t dataOffset, uint32_t dataLength)
  {
      ensureCondition(gas >= 0, ArgumentOutOfRange, "Negative gas supplied.");

      evmc_message call_message;
      call_message.destination = loadAddress(addressOffset);
      call_message.flags = m_msg.flags & EVMC_STATIC;
      call_message.depth = m_msg.depth + 1;

      switch (kind) {
      case EEICallKind::Call:
      case EEICallKind::CallCode:
        call_message.kind = (kind == EEICallKind::CallCode) ? EVMC_CALLCODE : EVMC_CALL;
        call_message.sender = m_msg.destination;
        call_message.value = loadUint128(valueOffset);

        if ((kind == EEICallKind::Call) && !evmc::is_zero(call_message.value)) {
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

#if HERA_DEBUGGING
      string methodName;
      switch (kind) {
      case EEICallKind::Call: methodName = "call"; break;
      case EEICallKind::CallCode: methodName = "callCode"; break;
      case EEICallKind::CallDelegate: methodName = "callDelegate"; break;
      case EEICallKind::CallStatic: methodName = "callStatic"; break;
      }

      HERA_DEBUG <<
        depthToString() << " " <<
        methodName << " " << hex <<
        gas << " " <<
        addressOffset << " " <<
        valueOffset << " " <<
        dataOffset << " " <<
        dataLength << dec << "\n";
#endif

      // NOTE: this must be declared outside the condition to ensure the memory doesn't go out of scope
      bytes input_data;
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

      // Start with base call gas
      takeInterfaceGas(GasSchedule::call);

      if (m_msg.depth >= 1024)
        return 1;

      // These checks are in EIP150 but not in the YellowPaper
      // Charge valuetransfer gas if value is being transferred.
      if ((kind == EEICallKind::Call || kind == EEICallKind::CallCode) && !evmc::is_zero(call_message.value)) {
        takeInterfaceGas(GasSchedule::valuetransfer);

        if (!enoughSenderBalanceFor(call_message.value))
          return 1;

        // Only charge callNewAccount gas if the account is new and non-zero value is being transferred per EIP161.
        if ((kind == EEICallKind::Call) && !m_host.account_exists(call_message.destination))
          takeInterfaceGas(GasSchedule::callNewAccount);
      }

      // This is the gas we are forwarding to the callee.
      // Retain one 64th of it as per EIP150
      gas = min(gas, maxCallGas(m_result.gasLeft));

      takeInterfaceGas(gas);

      // Add gas stipend for value transfers
      if (!evmc::is_zero(call_message.value))
        gas += GasSchedule::valueStipend;

      call_message.gas = gas;

      auto call_result = m_host.call(call_message);

      if (call_result.output_data) {
        m_lastReturnData.assign(call_result.output_data, call_result.output_data + call_result.output_size);
      } else {
        m_lastReturnData.clear();
      }

      /* Return unspent gas */
      heraAssert(call_result.gas_left >= 0, "EVMC returned negative gas left");
      m_result.gasLeft += call_result.gas_left;

      switch (call_result.status_code) {
      case EVMC_SUCCESS:
        return 0;
      case EVMC_REVERT:
        return 2;
      default:
        return 1;
      }
  }

  uint32_t EthereumInterface::eeiCreate(uint32_t valueOffset, uint32_t dataOffset, uint32_t length, uint32_t resultOffset)
  {
      HERA_DEBUG << depthToString() << " create " << hex << valueOffset << " " << dataOffset << " " << length << dec << " " << resultOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::create);

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "create");

      evmc_message create_message;

      create_message.destination = {};
      create_message.sender = m_msg.destination;
      create_message.value = loadUint128(valueOffset);

      if (m_msg.depth >= 1024)
        return 1;
      if (!enoughSenderBalanceFor(create_message.value))
        return 1;

      // NOTE: this must be declared outside the condition to ensure the memory doesn't go out of scope
      bytes contract_code;
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

      create_message.depth = m_msg.depth + 1;
      create_message.kind = EVMC_CREATE;
      create_message.flags = 0;

      int64_t gas = maxCallGas(m_result.gasLeft);
      create_message.gas = gas;
      takeInterfaceGas(gas);

      auto create_result = m_host.call(create_message);

      /* Return unspent gas */
      heraAssert(create_result.gas_left >= 0, "EVMC returned negative gas left");
      m_result.gasLeft += create_result.gas_left;

      if (create_result.status_code == EVMC_SUCCESS) {
        storeAddress(create_result.create_address, resultOffset);
        m_lastReturnData.clear();
      } else if (create_result.output_data) {
        m_lastReturnData.assign(create_result.output_data, create_result.output_data + create_result.output_size);
      } else {
        m_lastReturnData.clear();
      }

      switch (create_result.status_code) {
      case EVMC_SUCCESS:
        return 0;
      case EVMC_REVERT:
        return 2;
      default:
        return 1;
      }
  }

  void EthereumInterface::eeiSelfDestruct(uint32_t addressOffset)
  {
      HERA_DEBUG << depthToString() << " selfDestruct " << hex << addressOffset << dec << "\n";

      takeInterfaceGas(GasSchedule::selfdestruct);

      ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation, "selfDestruct");

      evmc_address address = loadAddress(addressOffset);

      if (!m_host.account_exists(address))
        takeInterfaceGas(GasSchedule::callNewAccount);

      m_host.selfdestruct(m_msg.destination, address);

      throw EndExecution{};
  }

  void EthereumInterface::takeGas(int64_t gas)
  {
    // NOTE: gas >= 0 is validated by the callers of this method
    ensureCondition(gas <= m_result.gasLeft, OutOfGas, "Out of gas.");
    m_result.gasLeft -= gas;
  }

  void EthereumInterface::takeInterfaceGas(int64_t gas)
  {
    if (!m_meterGas)
      return;
    heraAssert(gas >= 0, "Trying to take negative gas.");
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
    // NOTE: the source bound check is not needed as the caller already ensures it
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(memorySize() >= (srcOffset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[length - (i + 1)] = memoryGet(srcOffset + i);
    }
  }

  void EthereumInterface::loadMemory(uint32_t srcOffset, uint8_t *dst, size_t length)
  {
    // NOTE: the source bound check is not needed as the caller already ensures it
    ensureCondition((srcOffset + length) >= srcOffset, InvalidMemoryAccess, "Out of bounds (source) memory copy.");
    ensureCondition(memorySize() >= (srcOffset + length), InvalidMemoryAccess, "Out of bounds (source) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory load from offset 0x" << hex << srcOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      dst[i] = memoryGet(srcOffset + i);
    }
  }

  void EthereumInterface::loadMemory(uint32_t srcOffset, bytes& dst, size_t length)
  {
    // NOTE: the source bound check is not needed as the caller already ensures it
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

  void EthereumInterface::storeMemory(const uint8_t *src, uint32_t dstOffset, uint32_t length)
  {
    ensureCondition((dstOffset + length) >= dstOffset, InvalidMemoryAccess, "Out of bounds (destination) memory copy.");
    ensureCondition(memorySize() >= (dstOffset + length), InvalidMemoryAccess, "Out of bounds (destination) memory copy.");

    if (!length)
      HERA_DEBUG << "Zero-length memory store to offset 0x" << hex << dstOffset << dec << "\n";

    for (uint32_t i = 0; i < length; ++i) {
      memorySet(dstOffset + i, src[i]);
    }
  }

  void EthereumInterface::storeMemory(bytes_view src, uint32_t srcOffset, uint32_t dstOffset, uint32_t length)
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

  evmc::bytes32 EthereumInterface::loadBytes32(uint32_t srcOffset)
  {
    evmc::bytes32 dst;
    loadMemory(srcOffset, dst.bytes, 32);
    return dst;
  }

  void EthereumInterface::storeBytes32(evmc::uint256be const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 32);
  }

  evmc::uint256be EthereumInterface::loadUint256(uint32_t srcOffset)
  {
    evmc::uint256be dst;
    loadMemoryReverse(srcOffset, dst.bytes, 32);
    return dst;
  }

  void EthereumInterface::storeUint256(evmc::uint256be const& src, uint32_t dstOffset)
  {
    storeMemoryReverse(src.bytes, dstOffset, 32);
  }

  evmc::address EthereumInterface::loadAddress(uint32_t srcOffset)
  {
    evmc::address dst;
    loadMemory(srcOffset, dst.bytes, 20);
    return dst;
  }

  void EthereumInterface::storeAddress(evmc::address const& src, uint32_t dstOffset)
  {
    storeMemory(src.bytes, dstOffset, 20);
  }

  evmc::uint256be EthereumInterface::loadUint128(uint32_t srcOffset)
  {
    evmc::uint256be dst;
    loadMemoryReverse(srcOffset, dst.bytes + 16, 16);
    return dst;
  }

  void EthereumInterface::storeUint128(evmc::uint256be const& src, uint32_t dstOffset)
  {
    ensureCondition(!exceedsUint128(src), ArgumentOutOfRange, "Account balance (or transaction value) exceeds 128 bits.");
    storeMemoryReverse(src.bytes + 16, dstOffset, 16);
  }

  /*
   * Utilities
   */
  void EthereumInterface::safeChargeDataCopy(uint32_t length, unsigned baseCost) {
    takeInterfaceGas(baseCost);

    // Since length here is 32 bits divided by 32 (aka shifted right by 5 bits), we
    // can assume the upper bound for values is 27 bits.
    //
    // Since `gas` is 63 bits wide, that means we have an extra 36 bits of headroom.
    //
    // Allow 16 bits here.
    static_assert(GasSchedule::copy <= 65536, "Gas cost of copy could lead to overflow");
    // Using uint64_t to force a type issue if the underlying API changes.
    takeInterfaceGas(GasSchedule::copy * ((int64_t(length) + 31) / 32));
  }

  bool EthereumInterface::enoughSenderBalanceFor(evmc_uint256be const& value)
  {
    evmc_uint256be balance = m_host.get_balance(m_msg.destination);
    return safeLoadUint128(balance) >= safeLoadUint128(value);
  }

  unsigned __int128 EthereumInterface::safeLoadUint128(evmc_uint256be const& value)
  {
    ensureCondition(!exceedsUint128(value), ArgumentOutOfRange, "Account balance (or transaction value) exceeds 128 bits.");
    unsigned __int128 ret = 0;
    for (unsigned i = 16; i < 32; i++) {
      ret <<= 8;
      ret |= value.bytes[i];
    }
    return ret;
  }
}
