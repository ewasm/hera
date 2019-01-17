
#pragma once

#include <evmc/helpers.hpp>

inline bool is_zero(const evmc_address& address) noexcept
{
    return address == evmc_address{};
}

inline bool is_zero(const evmc_bytes32& x) noexcept
{
    return x == evmc_bytes32{};
}

inline bool exeeds_128bits(const evmc_bytes32& x) noexcept
{
    constexpr auto full128 = evmc_bytes32{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    return full128 < x;
}
