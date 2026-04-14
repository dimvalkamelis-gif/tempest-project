#pragma once

#include <string>
#include <unordered_map>

namespace tempest::core {

// Very small config loader:
// - Reads `tempest.conf` (key=value, # comments)
// - Does NOT store secrets (those live in `tempest.secrets` or env)
class Config {
public:
  bool load_file(const std::string& path, std::string* err);
  std::string get(const std::string& key, const std::string& def = "") const;
  bool get_bool(const std::string& key, bool def) const;
  int get_int(const std::string& key, int def) const;

private:
  std::unordered_map<std::string, std::string> kv_;
};

} // namespace tempest::core

