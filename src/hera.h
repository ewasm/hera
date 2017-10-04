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
  HeraCall(struct evm_context *_context, std::vector<char> _code, int64_t _gas, std::vector<char> _input)
  {
    context = _context;
    code = _code;
    gas = _gas;
    input = _input;
  }

public:
  struct evm_context *context;
  std::vector<char> code;
  int64_t gas;
  std::vector<char> input;

  std::vector<char> returnValue;
};

class Hera
{
public:
  /*Hera(struct evm_context *_context)
  {
    this->account_exists_fn = _context->fn_table.account_exists;
    this->get_storage_fn = _context->fn_table.get_storage;  
    this->set_storage_fn = _context->fn_table.set_storage;
    this->get_balance_fn = _context->fn_table.get_balance;
    this->get_code_fn = _context->fn_table.get_code;
    this->self_destruct_fn = _context->fn_table.selfdestruct;
    this->call_fn = _context->fn_table.call;  
    this->get_tx_context_fn = _context->fn_table.get_tx_context;  
    this->get_block_hash_fn = _context->fn_table.get_block_hash;  
    this->log_fn = _context->fn_table.log;
  }*/

  Hera(const int abi_vn,
       evm_destroy_fn destroy_fn,
       evm_execute_fn execute_fn,
       evm_get_code_status_fn get_code_status_fn,
       evm_prepare_code_fn prepare_code_fn,
       evm_set_option_fn set_option_fn) 
  {
    this->abi_version = abi_vn;
    this->destroy = destroy_fn;
    this->execute = execute_fn;
    this->get_code_status = get_code_status_fn;
    this->prepare_code = prepare_code_fn;
    this->evm_set_option = set_option_fn;
  }

  void execute(HeraCall *call);

public:
  const int abi_version;
  evm_destroy_fn destroy = nullptr;
  evm_execute_fn execute = nullptr;
  evm_get_code_status_fn get_code_status = nullptr;
  evm_prepare_code_fn prepare_code = nullptr;
  evm_set_option_fn set_option = nullptr;

/*
  evm_account_exists_fn account_exists_fn = nullptr;
  evm_get_storage_fn get_storage_fn = nullptr;
  evm_set_storage_fn set_storage_fn = nullptr;
  evm_get_balance_fn get_balance_fn = nullptr;
  evm_get_code_fn get_code_fn = nullptr;
  evm_selfdestruct_fn self_destruct_fn = nullptr;
  evm_call_fn call_fn = nullptr;
  evm_get_tx_context_fn get_tx_context_fn = nullptr;
  evm_get_block_hash_fn get_block_hash_fn = nullptr;
  evm_log_fn log_fn = nullptr;
*/
};

}
