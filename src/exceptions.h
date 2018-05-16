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

namespace HeraVM {

class OutOfGasException : public std::exception {
public:
  explicit OutOfGasException(std::string _msg):
    msg(std::move(_msg))
  {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

class ContractValidationFailure : public std::exception {
public:
  explicit ContractValidationFailure(std::string _msg):
    msg(std::move(_msg))
  {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

class InvalidMemoryAccess : public std::exception {
public:
  explicit InvalidMemoryAccess(std::string _msg):
    msg(std::move(_msg))
  {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

/// Static Mode Violation.
///
/// This exception is thrown when state modifying EEI function is called
/// in static mode.
class StaticModeViolation : public std::exception {
public:
  explicit StaticModeViolation(std::string const& _functionName):
    msg("Static mode violation in " + _functionName + ".")
  {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

class InternalErrorException : public std::exception {
public:
  explicit InternalErrorException(std::string _msg): msg(std::move(_msg)) {}
  const char* what() const noexcept override { return msg.c_str(); }
private:
  std::string msg;
};

#define heraAssert(condition, msg) { \
  if (!(condition)) throw HeraVM::InternalErrorException{msg}; \
}

#define ensureCondition(condition, ex, msg) { \
  if (!(condition)) throw HeraVM::ex{msg}; \
}

}
