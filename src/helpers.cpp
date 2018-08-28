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
