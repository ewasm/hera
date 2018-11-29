/*
 * Copyright 2018 Pawel Bylica
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

#include <hera/hera.h>
#include <evmc/helpers.h>
#include <initializer_list>
#include <utility>

class Hera
{
public:
    ~Hera() noexcept { m_instance->destroy(m_instance); }

    Hera(std::initializer_list<std::pair<const char*, const char*>> options)
      : m_instance{evmc_create_hera()}
    {
        for (auto option : options)
            evmc_set_option(m_instance, option.first, option.second);
    }

    evmc_result execute(
        evmc_revision rev, const evmc_message& msg, const uint8_t* code, size_t code_size) noexcept
    {
        return m_instance->execute(m_instance, nullptr, rev, &msg, code, code_size);
    }

private:
    evmc_instance* const m_instance = nullptr;
};

inline void expect(bool test) noexcept
{
    if (!test)
        __builtin_trap();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* input, size_t size)
{
    Hera heraBinaryen{{"engine", "binaryen"}};
    Hera heraWabt{{"engine", "wabt"}};

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.gas = 100000;

    auto resBinaryen = heraBinaryen.execute(EVMC_BYZANTIUM, msg, input, size);
    auto resWabt = heraBinaryen.execute(EVMC_BYZANTIUM, msg, input, size);

    expect(resBinaryen.gas_left == resWabt.gas_left);
    expect(resBinaryen.output_size == resWabt.output_size);

    if (resBinaryen.release)
        resBinaryen.release(&resBinaryen);
    if (resWabt.release)
        resWabt.release(&resWabt);

    return 0;
}
