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


#include <vector>
#include <queue>

#include <pass.h>
#include <wasm.h>
#include <wasm-binary.h>
#include <wasm-builder.h>
#include <wasm-interpreter.h>
#include <wasm-printing.h>
#include <wasm-validator.h>

#include "binaryen.h"
#include "debugging.h"
#include "eei.h"
#include "exceptions.h"

#include "shell-interface.h"

using namespace std;

namespace hera {

class BinaryenEthereumInterface : public wasm::ShellExternalInterface, EthereumInterface {
public:
  explicit BinaryenEthereumInterface(
    evmc_context* _context,
    vector<uint8_t> const& _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    ShellExternalInterface(),
    EthereumInterface(_context, _code, _msg, _result, _meterGas)
  { }

protected:
  wasm::Literal callImport(wasm::Import *import, wasm::LiteralList& arguments) override;
#if HERA_DEBUGGING
  wasm::Literal callDebugImport(wasm::Import *import, wasm::LiteralList& arguments);
#endif

  void importGlobals(map<wasm::Name, wasm::Literal>& globals, wasm::Module& wasm) override;

  void trap(const char* why) override {
    ensureCondition(false, VMTrap, why);
  }

private:
  size_t memorySize() const override { return memory.size(); }
  void memorySet(size_t offset, uint8_t value) override { memory.set<uint8_t>(offset, value); }
  uint8_t memoryGet(size_t offset) override { return memory.get<uint8_t>(offset); }

  wasm::Literal callImportFromEthereum( wasm::Import *import, wasm::LiteralList& arguments );
  wasm::Literal callImportFromEnv( wasm::Import *import, wasm::LiteralList& arguments );
};

  void BinaryenEthereumInterface::importGlobals(map<wasm::Name, wasm::Literal>& globals, wasm::Module& wasm) {
    (void)globals;
    (void)wasm;
    HERA_DEBUG << "importGlobals\n";
  }

#if HERA_DEBUGGING
  wasm::Literal BinaryenEthereumInterface::callDebugImport(wasm::Import *import, wasm::LiteralList& arguments) {
    heraAssert(import->module == wasm::Name("debug"), "Import namespace error.");

    if (import->base == wasm::Name("print32")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t value = static_cast<uint32_t>(arguments[0].geti32());

      cerr << "DEBUG print32: " << value << " " << hex << "0x" << value << dec << endl;

      return wasm::Literal();
    }

    if (import->base == wasm::Name("print64")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint64_t value = static_cast<uint64_t>(arguments[0].geti64());

      cerr << "DEBUG print64: " << value << " " << hex << "0x" << value << dec << endl;

      return wasm::Literal();
    }

    if (import->base == wasm::Name("printMem") || import->base == wasm::Name("printMemHex")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[1].geti32());

      debugPrintMem(import->base == wasm::Name("printMemHex"), offset, length);

      return wasm::Literal();
    }

    if (import->base == wasm::Name("printStorage") || import->base == wasm::Name("printStorageHex")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());

      debugPrintStorage(import->base == wasm::Name("printStorageHex"), pathOffset);

      return wasm::Literal();
    }

    if (import->base == wasm::Name("evmTrace")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pc = static_cast<uint32_t>(arguments[0].geti32());
      int32_t opcode = arguments[1].geti32();
      uint32_t cost = static_cast<uint32_t>(arguments[2].geti32());
      int32_t sp = arguments[3].geti32();

      debugEvmTrace(pc, opcode, cost, sp);

      return wasm::Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + " arguments)");
  }
#endif

  wasm::Literal BinaryenEthereumInterface::callImportFromEthereum( wasm::Import *import, wasm::LiteralList& arguments ) {
      if (import->base == wasm::Name("useGas")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          int64_t gas = arguments[0].geti64();

          eeiUseGas(gas);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getGasLeft")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetGasLeft());
      }

      if (import->base == wasm::Name("getAddress")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetAddress(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getExternalBalance")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

          eeiGetExternalBalance(addressOffset, resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getBlockHash")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint64_t number = static_cast<uint64_t>(arguments[0].geti64());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

          return wasm::Literal(eeiGetBlockHash(number, resultOffset));
      }

      if (import->base == wasm::Name("getCallDataSize")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetCallDataSize());
      }

      if (import->base == wasm::Name("callDataCopy")) {
          heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t dataOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[2].geti32());

          eeiCallDataCopy(resultOffset, dataOffset, length);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getCaller")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetCaller(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getCallValue")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetCallValue(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("codeCopy")) {
          heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t codeOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[2].geti32());

          eeiCodeCopy(resultOffset, codeOffset, length);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getCodeSize")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetCodeSize());
      }

      if (import->base == wasm::Name("externalCodeCopy")) {
          heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

          uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t codeOffset = static_cast<uint32_t>(arguments[2].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[3].geti32());

          eeiExternalCodeCopy(addressOffset, resultOffset, codeOffset, length);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getExternalCodeSize")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());

          return wasm::Literal(eeiGetExternalCodeSize(addressOffset));
      }

      if (import->base == wasm::Name("getBlockCoinbase")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetBlockCoinbase(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getBlockDifficulty")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetBlockDifficulty(offset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getBlockGasLimit")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetBlockGasLimit());
      }

      if (import->base == wasm::Name("getTxGasPrice")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t valueOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetTxGasPrice(valueOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("log")) {
          heraAssert(arguments.size() == 7, string("Argument count mismatch in: ") + import->base.str);

          uint32_t dataOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t numberOfTopics = static_cast<uint32_t>(arguments[2].geti32());
          uint32_t topic1 = static_cast<uint32_t>(arguments[3].geti32());
          uint32_t topic2 = static_cast<uint32_t>(arguments[4].geti32());
          uint32_t topic3 = static_cast<uint32_t>(arguments[5].geti32());
          uint32_t topic4 = static_cast<uint32_t>(arguments[6].geti32());

          eeiLog(dataOffset, length, numberOfTopics, topic1, topic2, topic3, topic4);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("getBlockNumber")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetBlockNumber());
      }

      if (import->base == wasm::Name("getBlockTimestamp")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetBlockTimestamp());
      }

      if (import->base == wasm::Name("getTxOrigin")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetTxOrigin(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("storageStore")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t valueOffset = static_cast<uint32_t>(arguments[1].geti32());

          eeiStorageStore(pathOffset, valueOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("storageLoad")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

          eeiStorageLoad(pathOffset, resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("finish")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t size = static_cast<uint32_t>(arguments[1].geti32());

          // This traps.
          eeiFinish(offset, size);
      }

      if (import->base == wasm::Name("revert")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t size = static_cast<uint32_t>(arguments[1].geti32());

          // This traps.
          eeiRevert(offset, size);
      }

      if (import->base == wasm::Name("getReturnDataSize")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetReturnDataSize());
      }

      if (import->base == wasm::Name("returnDataCopy")) {
          heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

          uint32_t dataOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t offset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t size = static_cast<uint32_t>(arguments[2].geti32());

          eeiReturnDataCopy(dataOffset, offset, size);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("call") ||
          import->base == wasm::Name("callCode") ||
          import->base == wasm::Name("callDelegate") ||
          import->base == wasm::Name("callStatic")
          ) {
          EEICallKind kind;
          if (import->base == wasm::Name("call"))
              kind = EEICallKind::Call;
          else if (import->base == wasm::Name("callCode"))
              kind = EEICallKind::CallCode;
          else if (import->base == wasm::Name("callDelegate"))
              kind = EEICallKind::CallDelegate;
          else if (import->base == wasm::Name("callStatic"))
              kind = EEICallKind::CallStatic;
          else
          heraAssert(false, "");

          if ((kind == EEICallKind::Call) || (kind == EEICallKind::CallCode)) {
              heraAssert(arguments.size() == 5, string("Argument count mismatch in: ") + import->base.str);
          } else {
              heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);
          }

          int64_t gas = arguments[0].geti64();
          uint32_t addressOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t valueOffset;
          uint32_t dataOffset;
          uint32_t dataLength;

          if (kind == EEICallKind::Call || kind == EEICallKind::CallCode) {
              valueOffset = static_cast<uint32_t>(arguments[2].geti32());
              dataOffset = static_cast<uint32_t>(arguments[3].geti32());
              dataLength = static_cast<uint32_t>(arguments[4].geti32());
          } else {
              valueOffset = 0;
              dataOffset = static_cast<uint32_t>(arguments[2].geti32());
              dataLength = static_cast<uint32_t>(arguments[3].geti32());
          }

          return wasm::Literal(eeiCall(kind, gas, addressOffset, valueOffset, dataOffset, dataLength));
      }

      if (import->base == wasm::Name("create")) {
          heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

          uint32_t valueOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t dataOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[2].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[3].geti32());

          return wasm::Literal(eeiCreate(valueOffset, dataOffset, length, resultOffset));
      }

      if (import->base == wasm::Name("selfDestruct")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());

          // This traps.
          eeiSelfDestruct(addressOffset);
      }

      heraAssert(false, string("1 Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + " arguments)");
  }

  // add by csun import rust api
  wasm::Literal BinaryenEthereumInterface::callImportFromEnv( wasm::Import *import, wasm::LiteralList& arguments ) {
      if (import->base == wasm::Name("input_length")) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiInputLength());
      }

      if (import->base == wasm::Name("fetch_input")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t inputOffset = static_cast<uint32_t>( arguments[0].geti32() );
          eeiFetchInput( inputOffset );

          return wasm::Literal();
      }

      if (import->base == wasm::Name("gas")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          int64_t gas = arguments[0].geti64();
          eeiUseGas(gas);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("value")) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());
          eeiGetValue( resultOffset );

          return wasm::Literal();
      }

      if (import->base == wasm::Name("storage_write")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t valueOffset = static_cast<uint32_t>(arguments[1].geti32());

          eeiStorageStore(pathOffset, valueOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("storage_read")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

          eeiStorageLoad(pathOffset, resultOffset);

          return wasm::Literal();
      }


      if (import->base == wasm::Name( "elog" )) {
          heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

          uint32_t topicOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t topicCount = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t dataOffset = static_cast<uint32_t>(arguments[2].geti32());
          uint32_t dataLength = static_cast<uint32_t>(arguments[3].geti32());
          uint32_t topic1 = 0, topic2 = 0, topic3 = 0, topic4 = 0;

          const uint32_t TOPIC_SIZE = 32;
          if ( topicCount > 1 ) topic1 = topicOffset;
          if ( topicCount > 2 ) topic2 = topic1 + TOPIC_SIZE;
          if ( topicCount > 3 ) topic3 = topic2 + TOPIC_SIZE;
          if ( topicCount > 4 ) topic4 = topic3 + TOPIC_SIZE;

          eeiLog(dataOffset, dataLength, topicCount, topic1, topic2, topic3, topic4);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("panic")) {
          /*heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);
          uint32_t payloadPtr = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t payloadLength = static_cast<uint32_t>(arguments[1].geti32());
          eeiPanic( payloadPtr, payloadLength );
          return wasm::Literal();*/

          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t size = static_cast<uint32_t>(arguments[1].geti32());

          eeiRevert(offset, size);
      }

      if (import->base == wasm::Name("ret")) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t size = static_cast<uint32_t>(arguments[1].geti32());

          eeiFinish( offset, size );
      }


      if (import->base == wasm::Name( "sender" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetCaller(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name( "create" )) {
          heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

          uint32_t valueOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t dataOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[2].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[3].geti32());

          return wasm::Literal(eeiCreate(valueOffset, dataOffset, length, resultOffset));
      }

      if (import->base == wasm::Name( "blockhash" )) {
          heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

          uint64_t number = static_cast<uint64_t>(arguments[0].geti64());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

          return wasm::Literal(eeiGetBlockHash(number, resultOffset));
      }

      if (import->base == wasm::Name( "blocknumber" )) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetBlockNumber());
      }

      if (import->base == wasm::Name( "coinbase" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetBlockCoinbase(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name( "difficulty" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetBlockDifficulty(offset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name( "timestamp" )) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal(eeiGetBlockTimestamp());
      }

      if (import->base == wasm::Name( "address" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetAddress(resultOffset);

          return wasm::Literal();
      }


      if (import->base == wasm::Name( "origin" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetTxOrigin(resultOffset);

          return wasm::Literal();
      }

      if (import->base == wasm::Name("ccall") ||
          import->base == wasm::Name("dcall") ||
          import->base == wasm::Name("scall") ) {
          EEICallKind kind;
          if (import->base == wasm::Name("ccall"))
              kind = EEICallKind::CallCode;
          else if (import->base == wasm::Name("dcall"))
              kind = EEICallKind::CallDelegate;
          else if (import->base == wasm::Name("scall"))
              kind = EEICallKind::CallStatic;
          else
          heraAssert(false, "");

          if (kind == EEICallKind::Call) {
              heraAssert(arguments.size() == 7, string("Argument count mismatch in: ") + import->base.str);
          } else {
              heraAssert(arguments.size() == 6, string("Argument count mismatch in: ") + import->base.str);
          }

          int64_t gas = arguments[0].geti64();
          uint32_t addressOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t valueOffset;
          uint32_t dataOffset;
          uint32_t dataLength;

          // csun TODO ??? what will happen when resultxxx do not pass
          uint32_t resultOffset;
          uint32_t resultLength;

          if (kind == EEICallKind::Call || kind == EEICallKind::CallCode) {
              valueOffset = static_cast<uint32_t>(arguments[2].geti32());
              dataOffset = static_cast<uint32_t>(arguments[3].geti32());
              dataLength = static_cast<uint32_t>(arguments[4].geti32());
              resultOffset = static_cast<uint32_t>(arguments[5].geti32());
              resultLength = static_cast<uint32_t>(arguments[6].geti32());
          } else {
              valueOffset = 0;
              dataOffset = static_cast<uint32_t>(arguments[2].geti32());
              dataLength = static_cast<uint32_t>(arguments[3].geti32());
              resultOffset = static_cast<uint32_t>(arguments[4].geti32());
              resultLength = static_cast<uint32_t>(arguments[5].geti32());
          }

          return wasm::Literal(eeiCall(kind, gas, addressOffset, valueOffset, dataOffset, dataLength));
      }

      // TODO ??? not sure
      if (import->base == wasm::Name( "gaslimit" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          uint32_t dstOffset = static_cast<uint32_t>(arguments[0].geti32());

          eeiGetBlockGasLimit( dstOffset );
          return wasm::Literal();
      }

      // TODO ??? not sure
      if (import->base == wasm::Name( "gasleft" )) {
          heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

          return wasm::Literal( eeiGetGasLeft() );
      }

      // TODO ??? unsupport now
      if (import->base == wasm::Name( "create2" )) {
          heraAssert(arguments.size() == 5, string("Argument count mismatch in: ") + import->base.str);

          /*uint32_t valueOffset = static_cast<uint32_t>(arguments[0].geti32());
          uint32_t saltOffset = static_cast<uint32_t>(arguments[1].geti32());
          uint32_t dataOffset = static_cast<uint32_t>(arguments[2].geti32());
          uint32_t length = static_cast<uint32_t>(arguments[3].geti32());
          uint32_t resultOffset = static_cast<uint32_t>(arguments[4].geti32());*/

          //return wasm::Literal(eeiCreate(valueOffset, dataOffset, length, resultOffset));

          // unsupport now
      }

      // TODO ???
      if (import->base == wasm::Name( "suicide" )) {
          heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

          //uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());
          // do nothing
      }

      // TODO ???
      if (import->base == wasm::Name("debug")) {
          // do nothing
      }

      heraAssert(false, string("1 Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + " arguments)");
  }

  wasm::Literal BinaryenEthereumInterface::callImport(wasm::Import *import, wasm::LiteralList& arguments) {
#if HERA_DEBUGGING
    if (import->module == wasm::Name("debug"))
      // Reroute to debug namespace
      return callDebugImport(import, arguments);
#endif

    HERA_DEBUG << "call import func " << import->module.str << "::" << import->base.str << " (" << to_string(arguments.size()) << " arguments)";

    if ( import->module == wasm::Name( "ethereum" ) ) {

        return callImportFromEthereum( import, arguments );

    } else if ( import->module == wasm::Name( "env" ) ) {

        return callImportFromEnv( import, arguments );

    }

    heraAssert(false, string( "Module " ) + import->module.str + string( " is not support, Only imports from the 'ethereum or env' namespace are allowed.") );
  }

unique_ptr<WasmEngine> BinaryenEngine::create()
{
  return unique_ptr<WasmEngine>{new BinaryenEngine};
}

// Execute the contract through Binaryen.
ExecutionResult BinaryenEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  wasm::Module module;

  // Load module
  loadModule(code, module);

  // Print
  //wasm::WasmPrinter::printModule(&module);
  //HERA_DEBUG << "finish module" << "\n";

  // Validate
  HERA_DEBUG << "verifyContract module before init instance\n";
  verifyContract(module);
  HERA_DEBUG << "verifyContract successfully\n";

  // NOTE: DO NOT use the optimiser here, it will conflict with metering

  // Interpret
  ExecutionResult result;
  BinaryenEthereumInterface interface(context, state_code, msg, result, meterInterfaceGas);
  wasm::ModuleInstance instance(module, &interface);

  try {
    // call function main if original code is evm bytes
    // call function call if original code is wasm bytes
    wasm::Name main = code.size() == state_code.size() ? wasm::Name("call") : wasm::Name("main");
    wasm::LiteralList args;
    instance.callExport(main, args);
  } catch (EndExecution const&) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
  }

  return result;
}

void BinaryenEngine::loadModule(vector<uint8_t> const& code, wasm::Module & module)
{
  try {
    wasm::WasmBinaryBuilder parser(module, reinterpret_cast<vector<char> const&>(code), false);
    parser.read();
  } catch (wasm::ParseException const& e) {
    string msg = "Error in parsing WASM binary: '" + e.text + "'";
    if (e.line != size_t(-1))
      msg += " (at " + to_string(e.line) + ":" + to_string(e.col) + ")";
    ensureCondition(false, ContractValidationFailure, msg);
  }
}

void BinaryenEngine::verifyContract(vector<uint8_t> const& code)
{
  wasm::Module module;
  loadModule(code, module);
  verifyContract(module);
}

namespace {
wasm::FunctionType createFunctionType(vector<wasm::Type> params, wasm::Type result) {
  wasm::FunctionType ret;
  ret.params = move(params);
  ret.result = move(result);
  return ret;
}
}

void BinaryenEngine::verifyContractOfExportMain( wasm::Module & module ) {
    ensureCondition(
            module.getExportOrNull(wasm::Name("main")) != nullptr,
            ContractValidationFailure,
            "Contract entry point (\"main\") missing."
    );

    ensureCondition(
            module.getExportOrNull(wasm::Name("memory")) != nullptr,
            ContractValidationFailure,
            "Contract export (\"memory\") missing."
    );

    ensureCondition(
            module.exports.size() == 2,
            ContractValidationFailure,
            "Contract exports more than (\"main\") and (\"memory\")."
    );

    // The existence of this is ensured above.
    wasm::Export* main_export = module.getExport(wasm::Name("main"));

    wasm::Function* main_function = module.getFunctionOrNull(main_export->value);
    ensureCondition(
            main_function,
            ContractValidationFailure,
            "Contract is invalid. \"main\" is not a function."
    );

    ensureCondition(
            (main_function->getNumParams() == 0) &&
            (main_function->result == wasm::Type::none),
            ContractValidationFailure,
            "Contract is invalid. \"main\" has an invalid signature."
    );

    static const map<wasm::Name const, wasm::FunctionType const> eei_signatures{
            { wasm::Name("useGas"), createFunctionType({ wasm::Type::i64 }, wasm::Type::none) },
            { wasm::Name("getGasLeft"), createFunctionType({}, wasm::Type::i64) },
            { wasm::Name("getAddress"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getExternalBalance"), createFunctionType({ wasm::Type::i32,wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getBlockHash"), createFunctionType({ wasm::Type::i64, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("getCallDataSize"), createFunctionType({}, wasm::Type::i32) },
            { wasm::Name("callDataCopy"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getCaller"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getCallValue"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("codeCopy"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32}, wasm::Type::none) },
            { wasm::Name("getCodeSize"), createFunctionType({}, wasm::Type::i32) },
            { wasm::Name("externalCodeCopy"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getExternalCodeSize"), createFunctionType({ wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("getBlockCoinbase"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getBlockDifficulty"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getBlockGasLimit"), createFunctionType({}, wasm::Type::i64) },
            { wasm::Name("getTxGasPrice"), createFunctionType({ wasm::Type::i32}, wasm::Type::none) },
            { wasm::Name("log"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getBlockNumber"), createFunctionType({}, wasm::Type::i64) },
            { wasm::Name("getBlockTimestamp"), createFunctionType({}, wasm::Type::i64) },
            { wasm::Name("getTxOrigin"), createFunctionType({ wasm::Type::i32}, wasm::Type::none) },
            { wasm::Name("storageStore"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("storageLoad"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("finish"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("revert"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("getReturnDataSize"), createFunctionType({ }, wasm::Type::i32) },
            { wasm::Name("returnDataCopy"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("call"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("callCode"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("callDelegate"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("callStatic"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("create"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("selfDestruct"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) }
    };

    for (auto const& import: module.imports) {
        #if HERA_DEBUGGING
        if (import->module == wasm::Name("debug"))
            continue;
        #endif

        ensureCondition(
                import->module == wasm::Name("ethereum"),
                ContractValidationFailure,
                "Import from invalid namespace."
        );

        ensureCondition(
                eei_signatures.count(import->base),
                ContractValidationFailure,
                "Importing invalid EEI method."
        );
        // NOTE: needs to be a copy by value due to `structuralComparison` requiring a non-const input
        wasm::FunctionType eei_function_type = eei_signatures.at(import->base);

        wasm::FunctionType *function_type = module.getFunctionTypeOrNull(import->functionType);
        ensureCondition(
                function_type,
                ContractValidationFailure,
                "Imported function type is missing."
        );

        HERA_DEBUG << "import name " << import->name << ", function_type:" << function_type->result << ","
                   << function_type->params.size() << ", eei_function_type:" << eei_function_type.result << ","
                   << eei_function_type.params.size() << "\n";

        ensureCondition(
                function_type->structuralComparison(eei_function_type),
                ContractValidationFailure,
                "Imported1 function type mismatch."
        );
    }
}

void BinaryenEngine::verifyContractOfExportCall( wasm::Module &module ) {
    ensureCondition(
            module.getExportOrNull(wasm::Name("call")) != nullptr,
            ContractValidationFailure,
            "Contract entry point (\"call\") missing."
    );

    ensureCondition(
            module.exports.size() == 1,
            ContractValidationFailure,
            "Contract exports more than (\"call\")."
    );

    // The existence of this is ensured above.
    wasm::Export* call_export = module.getExport(wasm::Name("call"));

    wasm::Function* call_function = module.getFunctionOrNull(call_export->value);
    ensureCondition(
            call_function,
            ContractValidationFailure,
            "Contract is invalid. \"call\" is not a function."
    );

    ensureCondition(
            (call_function->getNumParams() == 0) &&
            (call_function->result == wasm::Type::none),
            ContractValidationFailure,
            "Contract is invalid. \"call\" has an invalid signature."
    );

    static const map<wasm::Name const, wasm::FunctionType const> eei_signatures{
            { wasm::Name("storage_read"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("storage_write"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("ret"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("gas"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("input_length"), createFunctionType({}, wasm::Type::i32) },
            { wasm::Name("fetch_input"), createFunctionType({wasm::Type::i32}, wasm::Type::none) },
            { wasm::Name("panic"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("debug"), createFunctionType({ wasm::Type::i32, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("ccall"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32}, wasm::Type::i32) },
            { wasm::Name("dcall"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32}, wasm::Type::i32) },
            { wasm::Name("scall"), createFunctionType({ wasm::Type::i64, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32}, wasm::Type::i32) },
            { wasm::Name("value"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("create"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("suicide"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("blockhash"), createFunctionType({ wasm::Type::i64, wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("blocknumber"), createFunctionType({}, wasm::Type::i64) },
            { wasm::Name("coinbase"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("difficulty"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("gaslimit"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("timestamp"), createFunctionType({}, wasm::Type::i64) },
            { wasm::Name("address"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("sender"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("origin"), createFunctionType({ wasm::Type::i32 }, wasm::Type::none) },
            { wasm::Name("elog"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32}, wasm::Type::none) },
            { wasm::Name("create2"), createFunctionType({ wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32, wasm::Type::i32 }, wasm::Type::i32) },
            { wasm::Name("gasleft"), createFunctionType({}, wasm::Type::i64) }
    };

    for (auto const& import: module.imports) {
        #if HERA_DEBUGGING
        if (import->module == wasm::Name("debug"))
            continue;
        #endif

        ensureCondition(
                import->module == wasm::Name("env"),
                ContractValidationFailure,
                "Import from invalid namespace."
        );

        if ( import->base == wasm::Name( "memory" ) ) {
            continue;
        }

        //HERA_DEBUG << "check import " << import->base << "\n";
        ensureCondition(
                eei_signatures.count(import->base),
                ContractValidationFailure,
                "Importing invalid EEI method."
        );
        // NOTE: needs to be a copy by value due to `structuralComparison` requiring a non-const input
        wasm::FunctionType eei_function_type = eei_signatures.at(import->base);

        wasm::FunctionType *function_type = module.getFunctionTypeOrNull(import->functionType);
        ensureCondition(
                function_type,
                ContractValidationFailure,
                "Imported function type is missing."
        );

        //HERA_DEBUG << "import name " << import->name << ", function_type:" << function_type->result << ","
        //           << function_type->params.size() << ", eei_function_type:" << eei_function_type.result << ","
        //           << eei_function_type.params.size() << "\n";

        ensureCondition(
                function_type->structuralComparison(eei_function_type),
                ContractValidationFailure,
                "Imported2 function type mismatch."
        );

        HERA_DEBUG << "finish check import " << import->base << "\n";
    }
}

void BinaryenEngine::verifyContract(wasm::Module & module)
{
  ensureCondition(
    wasm::WasmValidator().validate(module),
    ContractValidationFailure,
    "Module is not valid."
  );

  // NOTE: Most of this should be caught during deployment time by the Sentinel.
  ensureCondition(
    module.start.isNull(),
    ContractValidationFailure,
    "Contract contains start function."
  );

  // csun modify TODO ???  memory is not export
  if ( module.getExportOrNull(wasm::Name("main")) != nullptr ) {
      verifyContractOfExportMain( module );
  } else {
      verifyContractOfExportCall( module );
  }
}

}
