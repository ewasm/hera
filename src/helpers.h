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

#include <string>

#include <evmc/evmc.hpp>

namespace hera {

using bytes = std::basic_string<uint8_t>;
using bytes_view = std::basic_string_view<uint8_t>;

bytes loadFileContents(std::string const& path);

std::string toHex(evmc::uint256be const& value);

// Returns a formatted string (with prefix "0x") representing the bytes of an array.
std::string bytesAsHexStr(bytes_view bytes);

bytes parseHexString(std::string const& input);

bool hasWasmPreamble(bytes_view _input);

bool hasWasmVersion(bytes_view _input, uint8_t _version);

}
