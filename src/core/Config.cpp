#include "tempest/core/Config.h"

#include "tempest/core/Strings.h"

#include <fstream>

namespace tempest::core {

bool Config::load_file(const std::string& path, std::string* err) {
  std::ifstream in(path);
  if (!in) {
    if (err) *err = "failed to open config: " + path;
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));
    if (!k.empty()) kv_[k] = v;
  }
  if (err) err->clear();
  return true;
}

std::string Config::get(const std::string& key, const std::string& def) const {
  auto it = kv_.find(key);
  if (it == kv_.end()) return def;
  return it->second;
}

bool Config::get_bool(const std::string& key, bool def) const {
  const std::string v = get(key, "");
  if (v.empty()) return def;
  if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
  if (v == "0" || v == "false" || v == "off" || v == "no") return false;
  return def;
}

int Config::get_int(const std::string& key, int def) const {
  const std::string v = get(key, "");
  if (v.empty()) return def;
  try {
    return std::stoi(v);
  } catch (...) {
    return def;
  }
}

} // namespace tempest::core

