#pragma once

#include <string>

namespace tempest::core::ansi {

std::string clear_screen();
std::string reset_style();
std::string bold();
std::string dim();
std::string fg_red();
std::string fg_green();
std::string fg_yellow();
std::string fg_cyan();
std::string fg_magenta();
std::string fg_white();

} // namespace tempest::core::ansi

