#pragma once

#include <string>

namespace tempest::core {

struct BuildInfo {
  std::string version;
  std::string compiler;
  std::string build_type;
};

BuildInfo build_info();

} // namespace tempest::core

