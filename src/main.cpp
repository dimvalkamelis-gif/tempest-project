#include "tempest/app/CommandRouter.h"

#include "tempest/core/BuildInfo.h"
#include "tempest/core/Config.h"
#include "tempest/core/Strings.h"
#include "tempest/models/ModelManager.h"
#include "tempest/rag/RagEngine.h"
#include "tempest/library/LocalLibrary.h"
#include "tempest/telegram/TelegramGateway.h"
#include "tempest/ui/TerminalUI.h"
#include "tempest/wiki/WikiIngestor.h"

#include <iostream>
#include <string>
#include <vector>

static void print_usage() {
  std::cout << "tempest\n"
            << "  --help            show help\n"
            << "  --version         print version\n"
            << "  --exec \"<cmd>\"     run one command then exit (repeatable)\n"
            << "  --wiki-ingest <dump.bz2> [--out <dir>] [--max-pages N]\n"
            << "\n"
            << "If no flags are provided, starts an interactive REPL.\n";
}

int main(int argc, char** argv) {
  tempest::ui::TerminalUI ui;
  tempest::models::ModelManager models;
  tempest::rag::RagEngine rag;
  tempest::telegram::TelegramGateway telegram;
  const auto build = tempest::core::build_info();

  // Load non-secret config if present.
  tempest::core::Config cfg;
  std::string cfg_err;
  (void)cfg.load_file("tempest.conf", &cfg_err);

  rag.set_wiki_index_dir(cfg.get("wiki.index_dir", ".tempest_wiki_index"));
  tempest::library::LocalLibrary library(cfg.get("library.db_path", "tempest_local.db"));

  models.set_model_path(tempest::models::ModelId::OneBit, cfg.get("model.path.1bit", "1bit_model/model.gguf"));
  models.set_model_path(tempest::models::ModelId::Tiny08B, cfg.get("model.path.0_8b", "0.8b_model/model.gguf"));
  models.set_model_path(tempest::models::ModelId::Reasoning3B, cfg.get("model.path.3b", "3b_model/model.gguf"));
  models.set_auto_routing(cfg.get_bool("model.auto", true));

  std::vector<std::string> exec_cmds;
  bool do_wiki_ingest = false;
  tempest::wiki::WikiIngestConfig wiki_cfg;
  wiki_cfg.out_dir = ".tempest_wiki_index";
  wiki_cfg.max_pages = -1;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--version") {
      std::cout << build.version << "\n";
      return 0;
    }
    if (arg == "--exec") {
      if (i + 1 >= argc) {
        std::cerr << "error: --exec requires an argument\n";
        return 2;
      }
      exec_cmds.emplace_back(argv[++i]);
      continue;
    }
    if (arg == "--wiki-ingest") {
      if (i + 1 >= argc) {
        std::cerr << "error: --wiki-ingest requires a path to .bz2\n";
        return 2;
      }
      do_wiki_ingest = true;
      wiki_cfg.dump_bz2_path = argv[++i];
      continue;
    }
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "error: --out requires a directory\n";
        return 2;
      }
      wiki_cfg.out_dir = argv[++i];
      continue;
    }
    if (arg == "--max-pages") {
      if (i + 1 >= argc) {
        std::cerr << "error: --max-pages requires an integer\n";
        return 2;
      }
      wiki_cfg.max_pages = std::stoll(argv[++i]);
      continue;
    }
    std::cerr << "error: unrecognized argument: " << arg << "\n";
    std::cerr << "try: --help\n";
    return 2;
  }

  if (do_wiki_ingest) {
    ui.print_line("wikipedia ingest starting...");
    tempest::wiki::WikiIngestStats st;
    std::string err;
    if (!tempest::wiki::ingest_wikipedia(wiki_cfg, &st, &err)) {
      std::cerr << "wiki ingest failed: " << err << "\n";
      return 1;
    }
    ui.print_line("wikipedia ingest complete.");
    ui.print_line("  pages_seen=" + std::to_string(st.pages_seen));
    ui.print_line("  pages_indexed=" + std::to_string(st.pages_indexed));
    ui.print_line("  chunks_written=" + std::to_string(st.chunks_written));
    ui.print_line("  bytes_written=" + std::to_string(st.bytes_written));
    return 0;
  }

  tempest::app::CommandRouter router(ui, models, rag, telegram, library, build);

  ui.clear();
  ui.print_banner();
  ui.print_status("type /help for commands");
  ui.print_status("status: model=" + models.status().active_name);

  if (!exec_cmds.empty()) {
    for (const auto& cmd : exec_cmds) {
      router.handle_line(cmd);
      if (!router.state().running) break;
    }
    return 0;
  }

  std::string line;
  while (router.state().running) {
    if (!std::getline(std::cin, line)) {
      ui.print_line("stdin closed; exiting (tip: run with --exec for non-interactive use)");
      return 0;
    }
    router.handle_line(line);
  }
  return 0;
}

