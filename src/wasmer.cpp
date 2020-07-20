/* Licensed under the Apache License, Version 2.0 (the "License");
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

#include "wasmer.h"
#include "runtime-c-api/wasmer.hh"
#include <cstdio>
#include <vector>
#include <memory>
#include "debugging.h"
#include <functional>
#include <iostream>
#include <set>

using namespace std;

namespace hera
{
    class WasmerEthereumInterface : public EthereumInterface
    {
    public:
        explicit WasmerEthereumInterface(evmc::HostContext &_context, bytes_view _code, evmc_message const &_msg, ExecutionResult &_result, bool _meterGas)
            : EthereumInterface(_context, _code, _msg, _result, _meterGas)
        {
        }

        void setWasmMemory(const wasmer_memory_t *_wasmMemory) { m_wasmMemory = _wasmMemory; }

    private:
        // These assume that m_wasmMemory was set prior to execution.
        size_t memorySize() const override { return wasmer_memory_data_length(m_wasmMemory); }
        void memorySet(size_t offset, uint8_t value) override
        {
            auto data = wasmer_memory_data(m_wasmMemory);
            data[offset] = value;
        }
        uint8_t memoryGet(size_t offset) override
        {
            auto data = wasmer_memory_data(m_wasmMemory);
            return data[offset];
        }
        uint8_t *memoryPointer(size_t offset, size_t length) override
        {
            ensureCondition(memorySize() >= (offset + length), InvalidMemoryAccess, "Memory is shorter than requested segment");
            auto data = wasmer_memory_data(m_wasmMemory);
            return data + offset;
        }

        const wasmer_memory_t *m_wasmMemory;
    };

    unique_ptr<WasmEngine> WasmerEngine::create()
    {
        return unique_ptr<WasmEngine>{new WasmerEngine};
    }
    namespace
    {
        wasmer_value_tag i32[] = {wasmer_value_tag::WASM_I32};
        wasmer_value_tag i64[] = {wasmer_value_tag::WASM_I64};
        wasmer_value_tag i64_i32[] = {wasmer_value_tag::WASM_I64, wasmer_value_tag::WASM_I32};
        wasmer_value_tag i32_2[] = {wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32};
        wasmer_value_tag i32_3[] = {wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32};
        wasmer_value_tag i32_4[] = {wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32};
        wasmer_value_tag i32_7[] = {wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32, wasmer_value_tag::WASM_I32};

        // Function to print the most recent error string from Wasmer if we have them
        string getWasmerErrorString()
        {
            int error_len = wasmer_last_error_length();
            char *error_str = new char[(uint64_t)error_len];
            wasmer_last_error_message(error_str, error_len);
            string error(error_str, (unsigned long)error_len);
            delete[] error_str;
            return error;
        }

        wasmer_byte_array getNameArray(const char *name)
        {
            return wasmer_byte_array{(const uint8_t *)name, (uint32_t)strlen(name)};
        }

        WasmerEthereumInterface *getInterfaceFromVontext(wasmer_instance_context_t *ctx)
        {
            return (WasmerEthereumInterface *)wasmer_instance_context_data_get(ctx);
        }

        void eeiUseGas(wasmer_instance_context_t *ctx, int64_t gas)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiUseGas(gas);
        }
        int64_t eeiGetGasLeft(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetGasLeft();
        }
        void eeiGetAddress(wasmer_instance_context_t *ctx, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetAddress(resultOffset);
        }
        void eeiGetExternalBalance(wasmer_instance_context_t *ctx, uint32_t addressOffset, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetExternalBalance(addressOffset, resultOffset);
        }

        uint32_t eeiGetBlockHash(wasmer_instance_context_t *ctx, uint64_t number, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetBlockHash(number, resultOffset);
        }
        uint32_t eeiGetCallDataSize(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetCallDataSize();
        }
        void eeiCallDataCopy(wasmer_instance_context_t *ctx, uint32_t resultOffset, uint32_t dataOffset, uint32_t length)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiCallDataCopy(resultOffset, dataOffset, length);
        }
        void eeiGetCaller(wasmer_instance_context_t *ctx, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetCaller(resultOffset);
        }
        void eeiGetCallValue(wasmer_instance_context_t *ctx, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetCallValue(resultOffset);
        }
        void eeiCodeCopy(wasmer_instance_context_t *ctx, uint32_t resultOffset, uint32_t codeOffset, uint32_t length)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiCodeCopy(resultOffset, codeOffset, length);
        }
        uint32_t eeiGetCodeSize(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetCodeSize();
        }
        void eeiExternalCodeCopy(wasmer_instance_context_t *ctx, uint32_t addressOffset, uint32_t resultOffset, uint32_t codeOffset, uint32_t length)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiExternalCodeCopy(addressOffset, resultOffset, codeOffset, length);
        }
        uint32_t eeiGetExternalCodeSize(wasmer_instance_context_t *ctx, uint32_t addressOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetExternalCodeSize(addressOffset);
        }
        void eeiGetBlockCoinbase(wasmer_instance_context_t *ctx, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetBlockCoinbase(resultOffset);
        }
        void eeiGetBlockDifficulty(wasmer_instance_context_t *ctx, uint32_t offset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetBlockDifficulty(offset);
        }
        int64_t eeiGetBlockGasLimit(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetBlockGasLimit();
        }
        void eeiGetTxGasPrice(wasmer_instance_context_t *ctx, uint32_t valueOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetTxGasPrice(valueOffset);
        }
        void eeiLog(wasmer_instance_context_t *ctx, uint32_t dataOffset, uint32_t length, uint32_t numberOfTopics, uint32_t topic1, uint32_t topic2, uint32_t topic3, uint32_t topic4)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiLog(dataOffset, length, numberOfTopics, topic1, topic2, topic3, topic4);
        }
        int64_t eeiGetBlockNumber(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetBlockNumber();
        }
        int64_t eeiGetBlockTimestamp(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetBlockTimestamp();
        }
        void eeiGetTxOrigin(wasmer_instance_context_t *ctx, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiGetTxOrigin(resultOffset);
        }
        void eeiStorageStore(wasmer_instance_context_t *ctx, uint32_t pathOffset, uint32_t valueOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiStorageStore(pathOffset, valueOffset);
        }
        void eeiStorageLoad(wasmer_instance_context_t *ctx, uint32_t pathOffset, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiStorageLoad(pathOffset, resultOffset);
        }
        void eeiFinish(wasmer_instance_context_t *ctx, uint32_t offset, uint32_t size)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiFinish(offset, size);
        }
        void eeiRevert(wasmer_instance_context_t *ctx, uint32_t offset, uint32_t size)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiRevert(offset, size);
        }
        uint32_t eeiGetReturnDataSize(wasmer_instance_context_t *ctx)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiGetReturnDataSize();
        }
        void eeiReturnDataCopy(wasmer_instance_context_t *ctx, uint32_t dataOffset, uint32_t offset, uint32_t size)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiReturnDataCopy(dataOffset, offset, size);
        }
        uint32_t eeiCreate(wasmer_instance_context_t *ctx, uint32_t valueOffset, uint32_t dataOffset, uint32_t length, uint32_t resultOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            return interface->eeiCreate(valueOffset, dataOffset, length, resultOffset);
        }
        void eeiSelfDestruct(wasmer_instance_context_t *ctx, uint32_t addressOffset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->eeiSelfDestruct(addressOffset);
        }
#if HERA_DEBUGGING
        void print32(wasmer_instance_context_t *, uint32_t value)
        {
            HERA_DEBUG<< "DEBUG print32: " << value << " " << hex << "0x" << value << dec << endl;
        }
        void print64(wasmer_instance_context_t *, uint64_t value)
        {
             HERA_DEBUG<< "DEBUG print64: " << value << " " << hex << "0x" << value << dec << endl;
        }
        void printMem(wasmer_instance_context_t *ctx, uint32_t offset, uint32_t size)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->debugPrintMem(false, offset, size);
        }
        void printMemHex(wasmer_instance_context_t *ctx, uint32_t offset, uint32_t size)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->debugPrintMem(true, offset, size);
        }
        void printStorage(wasmer_instance_context_t *ctx, uint32_t offset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->debugPrintStorage(false, offset);
        }
        void printStorageHex(wasmer_instance_context_t *ctx, uint32_t offset)
        {
            auto interface = getInterfaceFromVontext(ctx);
            interface->debugPrintStorage(true, offset);
        }
#endif
        shared_ptr<vector<wasmer_import_t>> initImportes()
        {
            wasmer_byte_array ethModule = getNameArray("ethereum");
            shared_ptr<vector<wasmer_import_t>> imports(
                new vector<wasmer_import_t>{
                    {ethModule, getNameArray("useGas"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiUseGas, i64, 1, NULL, 0)},
                    {ethModule, getNameArray("getGasLeft"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetGasLeft, NULL, 0, i64, 1)},
                    {ethModule, getNameArray("getAddress"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetAddress, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("getExternalBalance"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetExternalBalance, i32_2, 2, NULL, 0)},
                    {ethModule, getNameArray("getBlockHash"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetBlockHash, i64_i32, 2, i32, 1)},
                    {ethModule, getNameArray("getCallDataSize"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetCallDataSize, NULL, 0, i32, 1)},
                    {ethModule, getNameArray("callDataCopy"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiCallDataCopy, i32_3, 3, i32, 1)},
                    {ethModule, getNameArray("getCaller"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetCaller, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("getCallValue"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetCallValue, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("codeCopy"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiCodeCopy, i32_3, 3, NULL, 0)},
                    {ethModule, getNameArray("getCodeSize"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetCodeSize, NULL, 0, i32, 1)},
                    {ethModule, getNameArray("externalCodeCopy"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiExternalCodeCopy, i32_4, 4, NULL, 0)},
                    {ethModule, getNameArray("getExternalCodeSize"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetExternalCodeSize, i32, 1, i32, 1)},
                    {ethModule, getNameArray("getBlockCoinbase"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetBlockCoinbase, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("getBlockDifficulty"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetBlockDifficulty, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("getBlockGasLimit"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetBlockGasLimit, NULL, 0, i64, 1)},
                    {ethModule, getNameArray("getTxGasPrice"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetTxGasPrice, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("log"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiLog, i32_7, 7, NULL, 0)},
                    {ethModule, getNameArray("getBlockNumber"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetBlockNumber, NULL, 0, i64, 1)},
                    {ethModule, getNameArray("getBlockTimestamp"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetBlockTimestamp, NULL, 0, i64, 1)},
                    {ethModule, getNameArray("getTxOrigin"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetTxOrigin, i32, 1, NULL, 0)},
                    {ethModule, getNameArray("storageStore"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiStorageStore, i32_2, 2, NULL, 0)},
                    {ethModule, getNameArray("storageLoad"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiStorageLoad, i32_2, 2, NULL, 0)},
                    {ethModule, getNameArray("finish"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiFinish, i32_2, 2, NULL, 0)},
                    {ethModule, getNameArray("revert"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiRevert, i32_2, 2, NULL, 0)},
                    {ethModule, getNameArray("getReturnDataSize"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiGetReturnDataSize, NULL, 0, i32, 1)},
                    {ethModule, getNameArray("returnDataCopy"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiReturnDataCopy, i32_3, 3, NULL, 0)},
                    {ethModule, getNameArray("create"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiCreate, i32_4, 4, NULL, 0)},
                    {ethModule, getNameArray("selfDestruct"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))eeiSelfDestruct, i32, 1, NULL, 0)},
                },
                [](auto p) {
                    for (size_t i = 0; i < p->size(); ++i)
                    {
                        wasmer_import_func_destroy((wasmer_import_func_t *)p->at(i).value.func);
                    }
                    delete p;
                });
#if HERA_DEBUGGING
            wasmer_byte_array debugModule = getNameArray("debug");
            imports->push_back(wasmer_import_t{debugModule, getNameArray("print32"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))print32, i32, 1, NULL, 0)});
            imports->push_back(wasmer_import_t{debugModule, getNameArray("print64"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))print64, i64, 1, NULL, 0)});
            imports->push_back(wasmer_import_t{debugModule, getNameArray("printMem"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))printMem, i32_2, 2, NULL, 0)});
            imports->push_back(wasmer_import_t{debugModule, getNameArray("printMemHex"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))printMemHex, i32_2, 2, NULL, 0)});
            imports->push_back(wasmer_import_t{debugModule, getNameArray("printStorage"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))printStorage, i32, 1, NULL, 0)});
            imports->push_back(wasmer_import_t{debugModule, getNameArray("printStorageHex"), wasmer_import_export_kind::WASM_FUNCTION,
                     .value.func = wasmer_import_func_new((void (*)(void *))printStorageHex, i32, 1, NULL, 0)});
#endif
            return imports;
        }
    } // namespace
    static const set<string> eeiFunctions{"useGas", "getGasLeft", "getAddress", "getExternalBalance", "getBlockHash", "getCallDataSize", "callDataCopy", "getCaller",
                                          "getCallValue", "codeCopy", "getCodeSize", "externalCodeCopy", "getExternalCodeSize", "getBlockCoinbase",
                                          "getBlockDifficulty", "getBlockGasLimit", "getTxGasPrice", "log", "getBlockNumber", "getBlockTimestamp", "getTxOrigin", "storageStore",
                                          "storageLoad", "finish", "revert", "getReturnDataSize", "returnDataCopy", "call", "callCode", "callDelegate", "callStatic", "create", "selfDestruct"};
    void WasmerEngine::verifyContract(bytes_view code)
    {
        auto codeData = new unsigned char[code.size()];
        memcpy(codeData, code.data(), code.size());
        wasmer_module_t *module;
        auto compile_result = wasmer_compile(&module, codeData, (unsigned int)code.size());

        ensureCondition(
            compile_result == wasmer_result_t::WASMER_OK, ContractValidationFailure, "Compile wasm failed.");
        wasmer_export_descriptors_t *exports;
        wasmer_export_descriptors(module, &exports);
        auto len = wasmer_export_descriptors_len(exports);
        for (int i = 0; i < len; ++i)
        {
            auto exportObj = wasmer_export_descriptors_get(exports, i);
            auto nameBytes = wasmer_export_descriptor_name(exportObj);
            string objectName((char *)nameBytes.bytes, nameBytes.bytes_len);
            if (objectName == "memory")
            { // multiple memories are not supported for wasmer 0.17.0
                ensureCondition(wasmer_export_descriptor_kind(exportObj) == wasmer_import_export_kind::WASM_MEMORY, ContractValidationFailure, "\"memory\" is not pointing to memory.");
            }
            else if (objectName == "main")
            {
                ensureCondition(wasmer_export_descriptor_kind(exportObj) == wasmer_import_export_kind::WASM_FUNCTION, ContractValidationFailure, "\"main\" is not pointing to function.");
            }
            else
            {
                ensureCondition(false, ContractValidationFailure, "Invalid export is present.");
            }
        }
        wasmer_export_descriptors_destroy(exports);
        wasmer_import_descriptors_t *imports;
        wasmer_import_descriptors(module, &imports);
        auto importsLength = wasmer_import_descriptors_len(imports);

        for (unsigned int i = 0; i < importsLength; ++i)
        {
            auto importObj = wasmer_import_descriptors_get(imports, i);
            auto moduleNameBytes = wasmer_import_descriptor_module_name(importObj);
            string moduleName((char *)moduleNameBytes.bytes, moduleNameBytes.bytes_len);
#if HERA_DEBUGGING
            if (moduleName == "debug")
                continue;
#endif
            ensureCondition(moduleName == "ethereum", ContractValidationFailure, "Import from invalid namespace.");
            auto nameBytes = wasmer_import_descriptor_name(importObj);
            string objectName((char *)nameBytes.bytes, nameBytes.bytes_len);
            ensureCondition(eeiFunctions.count(objectName), ContractValidationFailure, "Importing invalid EEI method.");
            ensureCondition(wasmer_import_descriptor_kind(importObj) == wasmer_import_export_kind::WASM_FUNCTION, ContractValidationFailure, "Imported function type mismatch.");
        }
        wasmer_import_descriptors_destroy(imports);
        wasmer_module_destroy(module);
    }

    ExecutionResult WasmerEngine::execute(evmc::HostContext &context, bytes_view code, bytes_view state_code, evmc_message const &msg, bool meterInterfaceGas)
    {
        instantiationStarted();
        HERA_DEBUG << "Executing with wasmer...\n";
        // Set up interface to eei host functions
        ExecutionResult result;
        WasmerEthereumInterface interface{context, state_code, msg, result, meterInterfaceGas};
        // Define an array containing our imports
        auto imports = initImportes();
        // Instantiate a WebAssembly Instance from Wasm bytes and imports
        wasmer_instance_t *instance = NULL;
        auto codeData = new unsigned char[code.size()];
        memcpy(codeData, code.data(), code.size());
        wasmer_result_t compile_result =
            wasmer_instantiate(&instance,                                       // Our reference to our Wasm instance
                               codeData,                                        // The bytes of the WebAssembly modules
                               (uint32_t)code.size(),                           // The length of the bytes of the WebAssembly module
                               static_cast<wasmer_import_t *>(imports->data()), // The Imports array the will be used as our importObject
                               (int32_t)imports->size()                         // The number of imports in the imports array
            );

        ensureCondition(compile_result == wasmer_result_t::WASMER_OK, ContractValidationFailure, string("Compile wasm failed, ") + getWasmerErrorString());

        // Assert the Wasm instantion completed
        wasmer_instance_context_data_set(instance, (void *)&interface);
        auto ctx = wasmer_instance_context_get(instance);
        interface.setWasmMemory(wasmer_instance_context_memory(ctx, 0));
        // Call the Wasm function
        wasmer_result_t call_result = wasmer_instance_call(
            instance, // Our Wasm Instance
            "main",   // the name of the exported function we want to call on the guest Wasm module
            NULL,     // Our array of parameters
            0,        // The number of parameters
            NULL,     // Our array of results
            0         // The number of results
        );
        ensureCondition(call_result == wasmer_result_t::WASMER_OK, EndExecution, string("Call main failed, ") + getWasmerErrorString());

        wasmer_instance_destroy(instance);
        return result;
    };
} // namespace hera
