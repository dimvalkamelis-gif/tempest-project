#pragma once

#include "tempest/ui/UserInterface.h"
#include <string>

namespace tempest::ui {

class TerminalUI : public UserInterface {
public:
  TerminalUI() = default;

  void print_banner() const override;
  void print_status(const std::string& status) const override;
  void print_line(const std::string& s) const override;
  void print(const std::string& s) const override;
  void clear() const override;
};

} // namespace tempest::ui

