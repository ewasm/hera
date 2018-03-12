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

#ifndef __HERA_H
#define __HERA_H

#include <evmc/evmc.h>

#if defined _MSC_VER || defined __MINGW32__
# define HERA_EXPORT __declspec(dllexport)
# define HERA_IMPORT __declspec(dllimport)
#elif __GNU__ >= 4
# define HERA_EXPORT __attribute__((visibility("default")))
# define HERA_IMPORT __attribute__((visibility("default")))
#else
# define HERA_EXPORT
# define HERA_IMPORT
#endif

#if __cplusplus
extern "C" {
#endif

HERA_EXPORT
struct evmc_instance* evmc_create_hera(void);

/*
 * Enum for WASM engine runtime option
 */
typedef enum wasm_vm {
#if WABT_SUPPORTED
  VM_WABT,
#endif
#if WAVM_SUPPORTED
  VM_WAVM,
#endif
  VM_BINARYEN
} wasm_vm;

#if __cplusplus
}
#endif

#endif
