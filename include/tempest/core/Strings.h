#pragma once

#include <string>
#include <vector>

namespace tempest::core {

std::string trim(std::string s);
bool starts_with(const std::string& s, const std::string& prefix);
std::vector<std::string> split_ws(const std::string& s);
std::string to_lower(std::string s);

} // namespace tempest::core

