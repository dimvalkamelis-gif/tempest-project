#include "tempest/core/Clock.h"

#include <sstream>

namespace tempest::core {

std::string format_uptime(std::chrono::milliseconds ms) {
  using namespace std::chrono;
  const auto secs = duration_cast<seconds>(ms);
  const auto mins = duration_cast<minutes>(secs);
  const auto hours = duration_cast<std::chrono::hours>(mins);

  const auto s = (secs - mins).count();
  const auto m = (mins - hours).count();
  const auto h = hours.count();

  std::ostringstream oss;
  oss << h << "h " << m << "m " << s << "s";
  return oss.str();
}

} // namespace tempest::core

