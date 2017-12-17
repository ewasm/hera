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

#include "evm.h"

namespace HeraVM {

class HeraCall
{
public:
  HeraCall(struct evm_env *_env, std::vector<char> _code, int64_t _gas, std::vector<char> _input, struct evm_uint256 _value)
  {
    env = _env;
    code = _code;
    gas = _gas;
    input = _input;
    value = _value;
  }

public:
  struct evm_env *env;

  std::vector<char> code;
  int64_t gas;
  std::vector<char> input;
  struct evm_uint256 value;

  std::vector<char> returnValue;
};

class Hera
{
public:
  Hera(evm_query_fn query_fn, evm_update_fn update_fn, evm_call_fn call_fn)
  {
    this->query_fn = query_fn;
    this->update_fn = update_fn;
    this->call_fn = call_fn;
  }

  Hera() {}

  void execute(HeraCall *call);

public:
  evm_query_fn query_fn = nullptr;
  evm_update_fn update_fn = nullptr;
  evm_call_fn call_fn = nullptr;
};

}
