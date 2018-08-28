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

#include <vector>
#include <iomanip>
#include <sstream>
#include <fstream>

#include <evmc/evmc.h>

#include "helpers.h"

using namespace std;

namespace hera {

string loadFileContents(string const& path)
{
  ifstream is(path);
  return string{(istreambuf_iterator<char>(is)), istreambuf_iterator<char>()};
}

string toHex(evmc_uint256be const& value) {
  ostringstream os;
  os << hex;
  for (auto b: value.bytes)
    os << setw(2) << setfill('0') << unsigned(b);
  return "0x" + os.str();
}

string bytesAsHexStr(const uint8_t *bytes, const size_t length) {
  stringstream ret;
  ret << hex << "0x";
  for (size_t i = 0; i < length; ++i) {
    ret << setw(2) << setfill('0') << static_cast<int>(bytes[i]);
  }
  return ret.str();
}

namespace {
bool nibble2value(unsigned input, unsigned& output) {
  if (input >= '0' && input <= '9') {
    output = input - '0';
    return true;
  } else if (input >= 'a' && input <= 'f') {
    output = input - 'a' + 10;
    return true;
  } else if (input >= 'A' && input <= 'F') {
    output = input - 'A' + 10;
    return true;
  }
  return false;
}
}

// Hand rolled hex parser, because cross platform error handling is
// more reliable than with strtol() and any of the built std function.
//
// Returns an empty vector if input is invalid (odd number of characters or invalid nibbles).
// Assumes input is whitespace free, therefore if input is non-zero long an empty output
// signals an error.
vector<uint8_t> parseHexString(const string& input) {
  size_t len = input.length();
  if (len % 2 != 0)
    return vector<uint8_t>{};
  vector<uint8_t> ret;
  for (size_t i = 0; i <= len - 2; i += 2) {
    unsigned lo, hi;
    if (!nibble2value(unsigned(input[i]), hi) || !nibble2value(unsigned(input[i + 1]), lo))
      return vector<uint8_t>{};
    ret.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return ret;
}

bool hasWasmPreamble(vector<uint8_t> const& _input) {
  return
    _input.size() >= 8 &&
    _input[0] == 0 &&
    _input[1] == 'a' &&
    _input[2] == 's' &&
    _input[3] == 'm' &&
    _input[4] == 1 &&
    _input[5] == 0 &&
    _input[6] == 0 &&
    _input[7] == 0;
}

}
