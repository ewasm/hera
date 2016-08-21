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

#pragma once

#include <stdexcept>
#include <wasm.h>
#include <shell-interface.h>
#include "hera.h"

using namespace wasm;

namespace HeraVM {

struct EthereumInterface : ShellExternalInterface {
  EthereumInterface(Hera *_hera, HeraCall *_call) : ShellExternalInterface(), hera(_hera), call(_call) { }

  Literal callImport(Import *import, LiteralList& arguments) override {
    if (import->module != Name("ethereum")) {
      throw std::runtime_error("Only imports from the 'ethereum' namespace are allowed.");
    }

    if (import->base == Name("useGas")) {
      std::cout << "usegas\n";
      return Literal();
    }

    if (import->base == Name("callDataSize")) {
      std::cout << "calldatasize " << call->input.size() << "\n";
      return Literal((uint32_t)0);
    }

    if (import->base == Name("callDataCopy")) {
      std::cout << "calldatacopy\n";
      return Literal();
    }

    if (import->base == Name("return")) {
      std::cout << "return\n";
      
      uint32_t offset = arguments[0].geti32();
      uint32_t size = arguments[1].geti32();

      call->returnValue.clear();
      for (uint32_t i = offset; i < offset + size; i++) {
        call->returnValue.push_back(memory.get<uint8_t>(i));
      }

      return Literal();
    }

    throw std::runtime_error(std::string("Unsupported import called: ") + import->module.str + "::" + import->base.str);
  }

private:
  Hera *hera;
  HeraCall *call;
};

}
