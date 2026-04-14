#include "tempest/ui/TerminalUI.h"

#include "tempest/core/Ansi.h"
#include "tempest/core/Banner.h"

#include <iostream>

namespace tempest::ui {

void TerminalUI::print_banner() const {
  std::cout << tempest::core::banner();
  std::cout << tempest::core::ansi::dim()
            << "Tempest v" << TEMPEST_VERSION_STRING
            << tempest::core::ansi::reset_style() << "\n";
  std::cout.flush();
}

void TerminalUI::print_status(const std::string& status) const {
  std::cout << tempest::core::ansi::dim() << status
            << tempest::core::ansi::reset_style() << "\n";
  std::cout.flush();
}

void TerminalUI::print_line(const std::string& s) const {
  std::cout << s << "\n";
  std::cout.flush();
}

void TerminalUI::print(const std::string& s) const {
  std::cout << s;
  std::cout.flush();
}

void TerminalUI::clear() const {
  std::cout << tempest::core::ansi::clear_screen();
  std::cout.flush();
}

} // namespace tempest::ui

