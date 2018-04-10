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

#include "evm-instructions.h"

using namespace std;

namespace HeraVM {

const map<uint8_t, string> evmInstructionNames =
{
  { 0x00, "STOP" },
  { 0x01, "ADD" },
  { 0x02, "MUL" },
  { 0x03, "DIV" },
  { 0x04, "SDIV" },
  { 0x05, "MOD" },
  { 0x06, "SMOD" },
  { 0x07, "ADDMOD" },
  { 0x08, "MULMOD" },
  { 0x09, "EXP" },
  { 0x0a, "SIGNEXTEND" },

  { 0x10, "LT" },
  { 0x11, "GT" },
  // TODO: add others

  { 0x20, "SHA3" },

  { 0x30, "ADDRESS" },
  // TODO: add others
};

}
