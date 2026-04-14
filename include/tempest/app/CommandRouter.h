#pragma once

#include <string>

namespace tempest::models { class ModelManager; }
namespace tempest::rag { class RagEngine; }
namespace tempest::telegram { class TelegramGateway; }
namespace tempest::ui { class UserInterface; }
namespace tempest::core { struct BuildInfo; }
namespace tempest::library { class LocalLibrary; }

namespace tempest::app {

struct AppState {
  bool running{true};
  bool telegram_mode{false};
  bool rag_enabled{true};
};

class CommandRouter {
public:
  CommandRouter(
      tempest::ui::UserInterface& ui,
      tempest::models::ModelManager& models,
      tempest::rag::RagEngine& rag,
      tempest::telegram::TelegramGateway& telegram,
      tempest::library::LocalLibrary& library,
      const tempest::core::BuildInfo& build);

  void handle_line(const std::string& line);
  const AppState& state() const { return state_; }

private:
  void cmd_help();
  void cmd_exit();
  void cmd_clear();
  void cmd_reset();
  void cmd_info();
  void cmd_switch();
  void cmd_auto(const std::string& arg);
  void cmd_telegram();
  void cmd_rag(const std::string& query);
  void cmd_add(const std::string& path);
  void cmd_watch(const std::string& dir);
  void cmd_watches();

  std::string compose_status_line() const;

  tempest::ui::UserInterface& ui_;
  tempest::models::ModelManager& models_;
  tempest::rag::RagEngine& rag_;
  tempest::telegram::TelegramGateway& telegram_;
  tempest::library::LocalLibrary& library_;
  const tempest::core::BuildInfo& build_;
  AppState state_{};
};

} // namespace tempest::app

