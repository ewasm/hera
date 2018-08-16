/*
 * Copyright 2016-2018 Alex Beregszaszi et al.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

namespace HeraVM {

class HeraException : public std::exception {
public:
  explicit HeraException(std::string _msg): msg(std::move(_msg)) {}
  const char* what() const noexcept override { return msg.c_str(); }
protected:
  std::string msg;
};

class InternalErrorException : public HeraException {
  using HeraException::HeraException;
};
class VMTrap : public HeraException {
  using HeraException::HeraException;
};
class OutOfGas : public HeraException {
  using HeraException::HeraException;
};
class ContractValidationFailure : public HeraException {
  using HeraException::HeraException;
};
class ArgumentOutOfRange : public HeraException {
  using HeraException::HeraException;
};
class InvalidMemoryAccess : public HeraException {
  using HeraException::HeraException;
};

class EndExecution : public std::exception {
public:
  explicit EndExecution() {}
  const char* what() const noexcept override { return ""; }
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
protected:
  std::string msg;
};

#define heraAssert(condition, msg) { \
  if (!(condition)) throw HeraVM::InternalErrorException{msg}; \
}

#define ensureCondition(condition, ex, msg) { \
  if (!(condition)) throw HeraVM::ex{msg}; \
}

}
