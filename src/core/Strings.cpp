#include "tempest/core/Strings.h"

#include <cctype>

namespace tempest::core {

std::string trim(std::string s) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

bool starts_with(const std::string& s, const std::string& prefix) {
  if (prefix.size() > s.size()) return false;
  return s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char ch : s) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(ch);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string to_lower(std::string s) {
  for (auto& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

} // namespace tempest::core

