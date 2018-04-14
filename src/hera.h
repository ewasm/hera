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

#include <iostream>

/* 
 * Debug output stream
 * At runtime this is silenced if vm options disable debug messages
 */
#if HERA_DEBUGGING
namespace HeraDebugging {
  class NullBuf: public std::streambuf
  {
  public:
    virtual int overflow(int c) { return c; }
  };

  class NullStream : public std::ostream
  {
  public:
    NullStream(): std::ostream(&m_sb) { }
  private:
    NullBuf m_sb;
  };

  class DebugStream 
  {
  public:
    DebugStream()
    { nullstream = NullStream(); }

    bool isDebug() { return debugmode; }

    void setDebug(bool _debug) 
    { 
      debugmode = _debug; 
    }

    std::ostream& getStream()
    {
      return (debugmode) ? std::cerr : static_cast<std::ostream&>(nullstream); 
    }

  private:
    NullStream nullstream;
    bool debugmode;
  };

  DebugStream hera_debug;

}
#define HeraDebug() hera_debug.getStream()
#endif

#if __cplusplus
extern "C" {
#endif

struct evm_instance;

HERA_EXPORT
struct evm_instance* hera_create(void);

#if __cplusplus
}
#endif

#endif
