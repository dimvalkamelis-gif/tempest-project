#pragma once

#include <string>

namespace tempest::ui {

class UserInterface {
public:
  virtual ~UserInterface() = default;

  virtual void print_banner() const = 0;
  virtual void print_status(const std::string& status) const = 0;
  virtual void print_line(const std::string& s) const = 0;
  virtual void print(const std::string& s) const = 0;
  virtual void clear() const = 0;
};

} // namespace tempest::ui
