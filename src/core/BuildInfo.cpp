#include "tempest/core/BuildInfo.h"

#include <sstream>

namespace tempest::core {

static std::string compiler_string() {
#if defined(__clang__)
  std::ostringstream oss;
  oss << "clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
  return oss.str();
#elif defined(__GNUC__)
  std::ostringstream oss;
  oss << "gcc " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
  return oss.str();
#elif defined(_MSC_VER)
  std::ostringstream oss;
  oss << "msvc " << _MSC_VER;
  return oss.str();
#else
  return "unknown";
#endif
}

static std::string build_type_string() {
#if defined(NDEBUG)
  return "Release";
#else
  return "Debug";
#endif
}

BuildInfo build_info() {
  BuildInfo info;
#ifdef TEMPEST_VERSION_STRING
  info.version = TEMPEST_VERSION_STRING;
#else
  info.version = "0.0.0";
#endif
  info.compiler = compiler_string();
  info.build_type = build_type_string();
  return info;
}

} // namespace tempest::core

