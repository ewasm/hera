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

using namespace std;
using namespace wasm;

namespace hera {

  void BinaryenEthereumInterface::importGlobals(std::map<Name, Literal>& globals, Module& wasm) {
    (void)globals;
    (void)wasm;
    HERA_DEBUG << "importGlobals\n";
  }

#if HERA_DEBUGGING
  Literal BinaryenEthereumInterface::callDebugImport(Import *import, LiteralList& arguments) {
    heraAssert(import->module == Name("debug"), "Import namespace error.");

    if (import->base == Name("print32")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t value = static_cast<uint32_t>(arguments[0].geti32());

      cerr << "DEBUG print32: " << value << " " << hex << "0x" << value << dec << endl;

      return Literal();
    }

    if (import->base == Name("print64")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint64_t value = static_cast<uint64_t>(arguments[0].geti64());

      cerr << "DEBUG print64: " << value << " " << hex << "0x" << value << dec << endl;

      return Literal();
    }

    if (import->base == Name("printMem") || import->base == Name("printMemHex")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[1].geti32());

      debugPrintMem(import->base == Name("printMemHex"), offset, length);

      return Literal();
    }

    if (import->base == Name("printStorage") || import->base == Name("printStorageHex")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());

      debugPrintStorage(import->base == Name("printStorageHex"), pathOffset);

      return Literal();
    }

    if (import->base == Name("evmTrace")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pc = static_cast<uint32_t>(arguments[0].geti32());
      int32_t opcode = arguments[1].geti32();
      uint32_t cost = static_cast<uint32_t>(arguments[2].geti32());
      int32_t sp = arguments[3].geti32();

      debugEvmTrace(pc, opcode, cost, sp);

      return Literal();
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + " arguments)");
  }
#endif

  Literal BinaryenEthereumInterface::callImport(Import *import, LiteralList& arguments) {
#if HERA_DEBUGGING
    if (import->module == Name("debug"))
      // Reroute to debug namespace
      return callDebugImport(import, arguments);
#endif

    heraAssert(import->module == Name("ethereum"), "Only imports from the 'ethereum' namespace are allowed.");

    if (import->base == Name("useGas")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      int64_t gas = arguments[0].geti64();

      eeiUseGas(gas);

      return Literal();
    }

    if (import->base == Name("getGasLeft")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetGasLeft());
    }

    if (import->base == Name("getAddress")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetAddress(resultOffset);

      return Literal();
    }

    if (import->base == Name("getExternalBalance")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

      eeiGetExternalBalance(addressOffset, resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint64_t number = static_cast<uint64_t>(arguments[0].geti64());
      uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

      return Literal(eeiGetBlockHash(number, resultOffset));
    }

    if (import->base == Name("getCallDataSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetCallDataSize());
    }

    if (import->base == Name("callDataCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t dataOffset = static_cast<uint32_t>(arguments[1].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[2].geti32());

      eeiCallDataCopy(resultOffset, dataOffset, length);

      return Literal();
    }

    if (import->base == Name("getCaller")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetCaller(resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallValue")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetCallValue(resultOffset);

      return Literal();
    }

    if (import->base == Name("codeCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t codeOffset = static_cast<uint32_t>(arguments[1].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[2].geti32());

      eeiCodeCopy(resultOffset, codeOffset, length);

      return Literal();
    }

    if (import->base == Name("getCodeSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetCodeSize());
    }

    if (import->base == Name("externalCodeCopy")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());
      uint32_t codeOffset = static_cast<uint32_t>(arguments[2].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[3].geti32());

      eeiExternalCodeCopy(addressOffset, resultOffset, codeOffset, length);

      return Literal();
    }

    if (import->base == Name("getExternalCodeSize")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());

      return Literal(eeiGetExternalCodeSize(addressOffset));
    }

    if (import->base == Name("getBlockCoinbase")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetBlockCoinbase(resultOffset);

      return Literal();
    }

    if (import->base == Name("getBlockDifficulty")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetBlockDifficulty(offset);

      return Literal();
    }

    if (import->base == Name("getBlockGasLimit")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetBlockGasLimit());
    }

    if (import->base == Name("getTxGasPrice")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t valueOffset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetTxGasPrice(valueOffset);

      return Literal();
    }

    if (import->base == Name("log")) {
      heraAssert(arguments.size() == 7, string("Argument count mismatch in: ") + import->base.str);

      uint32_t dataOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[1].geti32());
      uint32_t numberOfTopics = static_cast<uint32_t>(arguments[2].geti32());
      uint32_t topic1 = static_cast<uint32_t>(arguments[3].geti32());
      uint32_t topic2 = static_cast<uint32_t>(arguments[4].geti32());
      uint32_t topic3 = static_cast<uint32_t>(arguments[5].geti32());
      uint32_t topic4 = static_cast<uint32_t>(arguments[6].geti32());

      eeiLog(dataOffset, length, numberOfTopics, topic1, topic2, topic3, topic4);

      return Literal();
    }

    if (import->base == Name("getBlockNumber")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetBlockNumber());
    }

    if (import->base == Name("getBlockTimestamp")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetBlockTimestamp());
    }

    if (import->base == Name("getTxOrigin")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t resultOffset = static_cast<uint32_t>(arguments[0].geti32());

      eeiGetTxOrigin(resultOffset);

      return Literal();
    }

    if (import->base == Name("storageStore")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t valueOffset = static_cast<uint32_t>(arguments[1].geti32());

      eeiStorageStore(pathOffset, valueOffset);

      return Literal();
    }

    if (import->base == Name("storageLoad")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t pathOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t resultOffset = static_cast<uint32_t>(arguments[1].geti32());

      eeiStorageLoad(pathOffset, resultOffset);

      return Literal();
    }

    if (import->base == Name("finish")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t size = static_cast<uint32_t>(arguments[1].geti32());

      // This traps.
      eeiFinish(offset, size);
    }

    if (import->base == Name("revert")) {
      heraAssert(arguments.size() == 2, string("Argument count mismatch in: ") + import->base.str);

      uint32_t offset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t size = static_cast<uint32_t>(arguments[1].geti32());

      // This traps.
      eeiRevert(offset, size);
    }

    if (import->base == Name("getReturnDataSize")) {
      heraAssert(arguments.size() == 0, string("Argument count mismatch in: ") + import->base.str);

      return Literal(eeiGetReturnDataSize());
    }

    if (import->base == Name("returnDataCopy")) {
      heraAssert(arguments.size() == 3, string("Argument count mismatch in: ") + import->base.str);

      uint32_t dataOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t offset = static_cast<uint32_t>(arguments[1].geti32());
      uint32_t size = static_cast<uint32_t>(arguments[2].geti32());

      eeiReturnDataCopy(dataOffset, offset, size);

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

      return Literal(eeiCall(kind, gas, addressOffset, valueOffset, dataOffset, dataLength));
    }

    if (import->base == Name("create")) {
      heraAssert(arguments.size() == 4, string("Argument count mismatch in: ") + import->base.str);

      uint32_t valueOffset = static_cast<uint32_t>(arguments[0].geti32());
      uint32_t dataOffset = static_cast<uint32_t>(arguments[1].geti32());
      uint32_t length = static_cast<uint32_t>(arguments[2].geti32());
      uint32_t resultOffset = static_cast<uint32_t>(arguments[3].geti32());

      return Literal(eeiCreate(valueOffset, dataOffset, length, resultOffset));
    }

    if (import->base == Name("selfDestruct")) {
      heraAssert(arguments.size() == 1, string("Argument count mismatch in: ") + import->base.str);

      uint32_t addressOffset = static_cast<uint32_t>(arguments[0].geti32());

      // This traps.
      eeiSelfDestruct(addressOffset);
    }

    heraAssert(false, string("Unsupported import called: ") + import->module.str + "::" + import->base.str + " (" + to_string(arguments.size()) + "arguments)");
  }

// NOTE: This should be caught during deployment time by the Sentinel.
void BinaryenEngine::validate_contract(Module & module)
{
  ensureCondition(
    module.getExportOrNull(Name("main")) != nullptr,
    ContractValidationFailure,
    "Contract entry point (\"main\") missing."
  );

  ensureCondition(
    module.getExportOrNull(Name("memory")) != nullptr,
    ContractValidationFailure,
    "Contract export (\"memory\") missing."
  );

  ensureCondition(
    module.exports.size() == 2,
    ContractValidationFailure,
    "Contract exports more than (\"main\") and (\"memory\")."
  );

  for (auto const& import: module.imports) {
    ensureCondition(
      import->module == Name("ethereum")
#if HERA_DEBUGGING
      || import->module == Name("debug")
#endif
      ,
      ContractValidationFailure,
      "Import from invalid namespace."
    );
  }
}

std::unique_ptr<WasmEngine> BinaryenEngine::create()
{
  return std::unique_ptr<WasmEngine>{new BinaryenEngine};
}

// Execute the contract through Binaryen.
ExecutionResult BinaryenEngine::execute(
  evmc_context* context,
  vector<uint8_t> const& code,
  vector<uint8_t> const& state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {
  Module module;

  // Load module
  try {
    WasmBinaryBuilder parser(module, reinterpret_cast<vector<char> const&>(code), false);
    parser.read();
  } catch (ParseException const& e) {
    string msg = "Error in parsing WASM binary: '" + e.text + "'";
    if (e.line != size_t(-1))
      msg += " (at " + to_string(e.line) + ":" + to_string(e.col) + ")";
    ensureCondition(false, ContractValidationFailure, msg);
  }

  // Print
  // WasmPrinter::printModule(module);

  // Validate
  ensureCondition(
    WasmValidator().validate(module),
    ContractValidationFailure,
    "Module is not valid."
  );

  // NOTE: This should be caught during deployment time by the Sentinel.
  validate_contract(module);

  // NOTE: DO NOT use the optimiser here, it will conflict with metering

  // Interpret
  ExecutionResult result;
  BinaryenEthereumInterface interface(context, state_code, msg, result, meterInterfaceGas);
  ModuleInstance instance(module, &interface);

  try {
    Name main = Name("main");
    LiteralList args;
    instance.callExport(main, args);
  } catch (EndExecution const&) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
  }

  return result;
}

}
