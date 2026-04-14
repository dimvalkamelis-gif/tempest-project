#include "tempest/core/Ansi.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#  include <io.h>
#  define TEMPEST_ISATTY _isatty
#  define TEMPEST_FILENO _fileno
#else
#  include <unistd.h>
#  define TEMPEST_ISATTY isatty
#  define TEMPEST_FILENO fileno
#endif

namespace tempest::core::ansi {

static bool ansi_enabled() {
  if (std::getenv("NO_COLOR") != nullptr) return false;
  const char* term = std::getenv("TERM");
  if (term != nullptr && std::string(term) == "dumb") return false;
  return TEMPEST_ISATTY(TEMPEST_FILENO(stdout)) != 0;
}

static std::string code(const char* s) {
  return ansi_enabled() ? std::string(s) : std::string();
}

std::string clear_screen() { return code("\x1b[2J\x1b[H"); }
std::string reset_style() { return code("\x1b[0m"); }
std::string bold() { return code("\x1b[1m"); }
std::string dim() { return code("\x1b[2m"); }
std::string fg_red() { return code("\x1b[31m"); }
std::string fg_green() { return code("\x1b[32m"); }
std::string fg_yellow() { return code("\x1b[33m"); }
std::string fg_cyan() { return code("\x1b[36m"); }
std::string fg_magenta() { return code("\x1b[35m"); }
std::string fg_white() { return code("\x1b[37m"); }

} // namespace tempest::core::ansi

