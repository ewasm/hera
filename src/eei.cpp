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

      uint32_t gas = arguments[0].geti32();

      std::cout << gas << "\n";

      takeGas(gas);

      return Literal();
    }

    if (import->base == Name("getAddress")) {
      std::cout << "getAddress ";

      uint32_t resultOffset = arguments[0].geti32();

      std::cout << resultOffset << "\n";

      union evm_variant arg = { .int64 = 0 };
      union evm_variant ret = hera->query_fn(call->env, EVM_ADDRESS, arg);

      copyAddressToMemory(ret.address, resultOffset);

      return Literal();
    }

    if (import->base == Name("getCallDataSize")) {
      std::cout << "calldatasize " << call->input.size() << "\n";
      return Literal((uint32_t)call->input.size());
    }

    if (import->base == Name("callDataCopy")) {
      std::cout << "calldatacopy ";

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
