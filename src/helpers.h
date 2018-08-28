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

#include <vector>

#include <evmc/evmc.h>

namespace hera {

std::string loadFileContents(std::string const& path);

std::string toHex(evmc_uint256be const& value);

// Returns a formatted string (with prefix "0x") representing the bytes of an array.
std::string bytesAsHexStr(const uint8_t *bytes, const size_t length);

std::vector<uint8_t> parseHexString(std::string const& input);

bool hasWasmPreamble(std::vector<uint8_t> const& _input);

}
