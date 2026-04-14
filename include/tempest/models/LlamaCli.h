#pragma once

#include <string>

namespace tempest::models {

struct LlamaRunConfig {
  std::string runner_path;      // resolved at build-time if available (llama-simple)
  std::string model_path;       // .gguf
  std::string prompt;
  int max_tokens{256};
  float temperature{0.7f};
};

struct LlamaRunResult {
  int exit_code{0};
  std::string stdout_text;
  std::string stderr_text;
};

LlamaRunResult run_llama_cli(const LlamaRunConfig& cfg);

} // namespace tempest::models

