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

using namespace wasm;

namespace HeraVM {

Literal EthereumInterface::callImport(Import *import, LiteralList& arguments) {
    if (import->module != Name("ethereum")) {
      throw std::runtime_error("Only imports from the 'ethereum' namespace are allowed.");
    }

    if (import->base == Name("useGas")) {
      std::cout << "usegas ";

      uint64_t gas = arguments[0].geti64();

      std::cout << gas << "\n";

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getAddress")) {
      std::cout << "getAddress ";

      uint32_t resultOffset = arguments[0].geti32();

      std::cout << resultOffset << "\n";

      evm_uint160be address;
      //TODO: get address of executing account
      memWrite(resultOffset, address->bytes, 20);

      return Literal();
    }

    if (import->base == Name("getBalance")) {
      std::cout << "getBalance";

      uint32_t addressOffset = arguments[0].geti32();
      uint32_t resultOffset = arguments[1].geti32();

      std::cout << addressOffset << " " << resultOffset << "\n";

      struct evm_uint160be *address;
      memRead(addressOffset, address->bytes, 20);
      struct evm_uint256be *balance;
      hera->get_balance_fn(balance, call->context, address);
      memWrite(resultOffset, balance->bytes, 32);

      return Literal();
    }

    if (import->base == Name("getBlockHash")) {
      std::cout << "getBlockHash";

      uint64_t number = arguments[0].geti64();
      uint32_t resultOffset = arguments[1].geti32();
      std::cout << number << " " << resultOffset << "\n";
      
      struct evm_uint256be *blockhash;
      hera->get_block_hash_fn(blockhash, call->context, number);
      memWrite(resultOffset, blockhash->bytes, 32);

      return Literal();
    }

    if (import->base == Name("call")) {
      std::cout << "call";
      
      uint64_t gas = arguments[0].geti64();
      uint32_t addressOffset = arguments[1].geti32();
      uint32_t valueOffset = arguments[2].geti32();
      uint32_t dataOffset = arguments[3].geti32();
      uint32_t dataLength = arguments[4].geti32();
      uint32_t resultOffset = arguments[5].geti32();
      uint32_t resultLength = arguments[6].geti32();

      evm_uint160be *address;
      copyAddressFromMemory(address, addressOffset);
      evm_uint256 *value;
      copy256FromMemory(value, valueOffset);
      
      uint8_t trap_state;
      return Literal((uint8_t)trap_state);
    }

    if (import->base == Name("getCallDataSize")) {
      std::cout << "callDataSize " << call->input.size() << "\n";
      return Literal((uint32_t)call->input.size());
    }

    if (import->base == Name("callDataCopy")) {
      std::cout << "CallDataCopy ";

      uint32_t resultOffset = arguments[0].geti32();
      uint32_t dataOffset = arguments[1].geti32();
      uint32_t length = arguments[2].geti32();

      std::cout << resultOffset << " " << dataOffset << " " << length << "\n";

      memoryCopy(call->input, dataOffset, resultOffset, length);

      return Literal();
    }

    if (import->base == Name("return")) {
      std::cout << "return ";

      uint32_t offset = arguments[0].geti32();
      uint32_t size = arguments[1].geti32();

      std::cout << offset << " " << size << "\n";

      call->returnValue.clear();
      for (uint32_t i = offset; i < offset + size; i++) {
        call->returnValue.push_back(memory.get<uint8_t>(i));
      }

      return Literal();
    }

    throw std::runtime_error(std::string("Unsupported import called: ") + import->module.str + "::" + import->base.str);
  }

}
