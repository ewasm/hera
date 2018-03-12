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

#include "vm.h"

using namespace std;
using namespace wasm;
using namespace HeraVM;

void WasmVM::execute() 
{
	switch (this->vm) {
	case BINARYEN:
		this->exitStatus = this->runBinaryen();
		break;
	#ifdef WABT_SUPPORTED
	case WABT:
		this->exitStatus = this->runWabt();
		break;
	#endif
	#ifdef WAVM_SUPPORTED
	case WAVM:
		this->exitStatus = this->runWavm();
		break;
	#endif
	}
}

int WasmVM::runBinaryen()
{
	Module module;

	try {
		WasmBinaryBuilder parser(module, reinterpret_cast<vector<char> const&>(this->code), false);
		parser.read();
	} catch (ParseException &p) {
		return 1;
	}

	/* Validation */
	heraAssert(WasmValidator().validate(module), "Module is not valid.");
	heraAssert(module.getExportOrNull(Name("main")) != nullptr, 
					      "Contract entry point (\"main\") missing.");
	
	EthereumInterface interface(this->context,
				        this->code,
					this->msg,
					this->output);
	ModuleInstance instance(module, &interface);

	Name main = Name("main");
	LiteralList args;
	instance.callExport(main, args);

	return 0;
}
