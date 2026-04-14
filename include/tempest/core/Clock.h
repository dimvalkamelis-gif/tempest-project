#pragma once

#include <chrono>
#include <string>

namespace tempest::core {

using SteadyClock = std::chrono::steady_clock;

std::string format_uptime(std::chrono::milliseconds ms);

} // namespace tempest::core

