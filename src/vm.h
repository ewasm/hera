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

#ifndef __VM_H
#define __VM_H

#include "hera.h"
#include "eei.h" /* for ExecutionResult */

#include <evmc.h>

using namespace std;
using namespace HeraVM;

class WasmVM
{
private:
	wasm_vm vm;
	vector<uint8_t> code;
	evmc_message msg;
	evmc_context *context;

	int runBinaryen();
	#if WABT_SUPPORTED
	int runWabt();
	#endif
	#if WAVM_SUPPORTED
	int runWavm();
	#endif

public:
	WasmVM(wasm_vm const vm, 
	           vector<uint8_t> const& code, 
		   evm_message const& msg,
		   evm_context *context)
	{
		this->vm = vm;
		this->msg = msg;
		this->output.gasLeft = (uint64_t)msg.gas;
		this->context = context;
		for (size_t i = 0; i < code.size(); ++i)
			this->code.push_back(code[i]);
	}

	void execute();

	struct ExecutionResult output;
	int exitStatus;
};

#endif