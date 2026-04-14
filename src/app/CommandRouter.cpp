#include "tempest/app/CommandRouter.h"

#include "tempest/core/Ansi.h"
#include "tempest/core/BuildInfo.h"
#include "tempest/core/Clock.h"
#include "tempest/core/Strings.h"
#include "tempest/library/LocalLibrary.h"
#include "tempest/models/ModelManager.h"
#include "tempest/rag/RagEngine.h"
#include "tempest/telegram/TelegramGateway.h"
#include "tempest/ui/UserInterface.h"

#include <chrono>
#include <sstream>

namespace tempest::app {

static std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

CommandRouter::CommandRouter(
    tempest::ui::UserInterface& ui,
    tempest::models::ModelManager& models,
    tempest::rag::RagEngine& rag,
    tempest::telegram::TelegramGateway& telegram,
    tempest::library::LocalLibrary& library,
    const tempest::core::BuildInfo& build)
    : ui_(ui), models_(models), rag_(rag), telegram_(telegram), library_(library), build_(build) {}

std::string CommandRouter::compose_status_line() const {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - g_start);
  const auto st = models_.status();

  std::ostringstream oss;
  oss << tempest::core::ansi::fg_cyan() << "[tempest]" << tempest::core::ansi::reset_style()
      << " model=" << st.active_name
      << " rag=" << (state_.rag_enabled ? (rag_.available() ? "on" : "off(build)") : "off")
      << " " << telegram_.status_line()
      << " uptime=" << tempest::core::format_uptime(ms);
  return oss.str();
}

void CommandRouter::handle_line(const std::string& raw) {
  const std::string line = tempest::core::trim(raw);
  if (line.empty()) return;

  if (line == "/help") return cmd_help();
  if (line == "/exit") return cmd_exit();
  if (line == "/clear") return cmd_clear();
  if (line == "/reset") return cmd_reset();
  if (line == "/info") return cmd_info();
  if (line == "/switch") return cmd_switch();
  if (tempest::core::starts_with(line, "/auto")) {
    std::string a;
    if (line.size() > 5) a = tempest::core::trim(line.substr(5));
    return cmd_auto(a);
  }
  if (line == "/telegram") return cmd_telegram();
  if (tempest::core::starts_with(line, "/add")) {
    std::string p;
    if (line.size() > 4) p = tempest::core::trim(line.substr(4));
    return cmd_add(p);
  }
  if (tempest::core::starts_with(line, "/watch")) {
    std::string p;
    if (line.size() > 6) p = tempest::core::trim(line.substr(6));
    return cmd_watch(p);
  }
  if (line == "/watches") return cmd_watches();

  if (tempest::core::starts_with(line, "/rag")) {
    std::string query;
    if (line.size() > 4) query = tempest::core::trim(line.substr(4));
    return cmd_rag(query);
  }

  // Treat any non-command input as a chat prompt.
  std::string final_prompt = line;
  if (state_.rag_enabled) {
    const auto res = rag_.retrieve(line, 3);
    if (!res.chunks.empty()) {
      final_prompt = "USE THE FOLLOWING CONTEXT TO ANSWER THE USER REQUEST.\n"
                     "CONTEXT:\n" + res.synthesized_context + "\n"
                     "USER REQUEST: " + line;
    }
  }

  bool streamed = false;
  std::string response = models_.generate(final_prompt, [this, &streamed](const std::string& token) {
    ui_.print(token);
    streamed = true;
  });

  if (!streamed) {
    ui_.print_line(response);
  } else {
    ui_.print_line("");
  }
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_help() {
  ui_.print_line("commands:");
  ui_.print_line("  /help      show this help");
  ui_.print_line("  /exit      exit tempest");
  ui_.print_line("  /clear     clear screen");
  ui_.print_line("  /reset     reset session state");
  ui_.print_line("  /info      show build/system info");
  ui_.print_line("  /switch    hot-swap 0.8b <-> 3b");
  ui_.print_line("  /auto          toggle adaptive model routing");
  ui_.print_line("  /auto on|off   set adaptive model routing");
  ui_.print_line("  /rag           toggle RAG on/off");
  ui_.print_line("  /rag on|off    set RAG state");
  ui_.print_line("  /rag <q>       run RAG retrieval for question");
  ui_.print_line("  /telegram  toggle telegram mode (stub unless enabled)");
  ui_.print_line("  /add <p>   add .txt file/dir to local library (sqlite)");
  ui_.print_line("  /watch <d> watch dir (polling) and auto-index .txt");
  ui_.print_line("  /watches   list watched dirs");
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_exit() {
  state_.running = false;
  ui_.print_line("exiting...");
}

void CommandRouter::cmd_clear() {
  ui_.clear();
  ui_.print_banner();
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_reset() {
  state_ = AppState{};
  ui_.print_line("session reset.");
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_info() {
  const auto st = models_.status();
  ui_.print_line("Tempest");
  ui_.print_line("  version: " + build_.version);
  ui_.print_line("  compiler: " + build_.compiler);
  ui_.print_line("  build: " + build_.build_type);
  ui_.print_line("  model: " + st.active_name + (st.llama_enabled ? " (llama.cpp enabled)" : " (llama.cpp disabled)"));
  ui_.print_line(std::string("  rag: ") + (rag_.available() ? "available" : "not built"));
  ui_.print_line("  telegram: " + std::string(telegram_.available() ? "available" : "not built"));
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_switch() {
  models_.switch_model();
  ui_.print_line("switched model -> " + models_.status().active_name);
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_auto(const std::string& arg) {
  if (arg.empty()) {
    models_.set_auto_routing(!models_.auto_routing());
  } else if (arg == "on") {
    models_.set_auto_routing(true);
  } else if (arg == "off") {
    models_.set_auto_routing(false);
  } else {
    ui_.print_line("usage: /auto [on|off]");
    ui_.print_status(compose_status_line());
    return;
  }
  ui_.print_line(std::string("auto routing: ") + (models_.auto_routing() ? "on" : "off"));
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_telegram() {
  state_.telegram_mode = !state_.telegram_mode;
  ui_.print_line(std::string("telegram mode: ") + (state_.telegram_mode ? "on" : "off"));
  if (!telegram_.available()) {
    ui_.print_line("note: telegram integration not built; this toggles UI state only.");
    ui_.print_status(compose_status_line());
    return;
  }

  if (state_.telegram_mode) {
    std::string err;
    const bool ok = telegram_.start(
        [this](const tempest::telegram::TelegramMessage& m) {
          // Minimal behavior: if user sends "/rag ..." run retrieval and send top sources.
          std::string reply;
          const std::string t = tempest::core::trim(m.text);
          if (tempest::core::starts_with(t, "/rag")) {
            std::string q;
            if (t.size() > 4) q = tempest::core::trim(t.substr(4));
            if (q.empty()) {
              reply = "usage: /rag <question>";
            } else if (!state_.rag_enabled) {
              reply = "rag is off on the PC core. enable it with /rag in the terminal.";
            } else {
              const auto res = rag_.retrieve(q, 3);
              if (res.chunks.empty()) {
                reply = "no matches.";
              } else {
                reply = "Top sources:\n";
                for (size_t i = 0; i < res.chunks.size(); ++i) {
                  reply += "[" + std::to_string(i + 1) + "] " + res.chunks[i].title + "\n";
                }
                // Include a small snippet from the best chunk.
                reply += "\nSnippet:\n";
                std::string snip = res.chunks[0].text;
                if (snip.size() > 700) snip.resize(700);
                reply += snip;
              }
            }
          } else {
            reply = "Tempest core online. Try: /rag <question>";
          }

          std::string send_err;
          (void)telegram_.send_message(m.chat_id, reply, &send_err);
        },
        &err);
    if (!ok) {
      ui_.print_line("telegram start failed: " + err);
      state_.telegram_mode = false;
    } else {
      ui_.print_line("telegram polling started. set TEMPEST_TELEGRAM_TOKEN in env.");
    }
  } else {
    telegram_.stop();
    ui_.print_line("telegram polling stopped.");
  }
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_rag(const std::string& query) {
  if (query.empty()) {
    state_.rag_enabled = !state_.rag_enabled;
    ui_.print_line(std::string("rag: ") + (state_.rag_enabled ? "on" : "off"));
    ui_.print_status(compose_status_line());
    return;
  }

  if (query == "on") {
    state_.rag_enabled = true;
    ui_.print_line("rag: on");
    ui_.print_status(compose_status_line());
    return;
  }
  if (query == "off") {
    state_.rag_enabled = false;
    ui_.print_line("rag: off");
    ui_.print_status(compose_status_line());
    return;
  }

  if (!state_.rag_enabled) {
    ui_.print_line("rag is off. use /rag to enable.");
    ui_.print_status(compose_status_line());
    return;
  }

  const auto res = rag_.retrieve(query, 5);
  ui_.print_line("rag retrieved " + std::to_string(res.chunks.size()) + " chunks:");
  for (size_t i = 0; i < res.chunks.size(); ++i) {
    const auto& c = res.chunks[i];
    ui_.print_line("  [" + std::to_string(i + 1) + "] score=" + std::to_string(c.score) + " " + c.title);
  }
  if (!res.synthesized_context.empty()) {
    ui_.print_line("");
    ui_.print_line(res.synthesized_context);
  }
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_add(const std::string& path) {
  if (path.empty()) {
    ui_.print_line("usage: /add <path-to-txt-or-dir>");
    ui_.print_status(compose_status_line());
    return;
  }
  tempest::library::IndexStats st;
  std::string err;
  if (!library_.add_path(path, &st, &err)) {
    ui_.print_line("add failed: " + err);
    ui_.print_status(compose_status_line());
    return;
  }
  ui_.print_line("indexed local library: files=" + std::to_string(st.files_indexed) +
                 " chunks=" + std::to_string(st.chunks_written));
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_watch(const std::string& dir) {
  if (dir.empty()) {
    ui_.print_line("usage: /watch <directory>");
    ui_.print_status(compose_status_line());
    return;
  }
  std::string err;
  if (!library_.watch_dir(dir, &err)) {
    ui_.print_line("watch failed: " + err);
    ui_.print_status(compose_status_line());
    return;
  }
  ui_.print_line("watching: " + dir);
  ui_.print_status(compose_status_line());
}

void CommandRouter::cmd_watches() {
  const auto w = library_.watched_dirs();
  if (w.empty()) {
    ui_.print_line("watched dirs: (none)");
  } else {
    ui_.print_line("watched dirs:");
    for (const auto& d : w) ui_.print_line("  - " + d);
  }
  ui_.print_status(compose_status_line());
}

} // namespace tempest::app

