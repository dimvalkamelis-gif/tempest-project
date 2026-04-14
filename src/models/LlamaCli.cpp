#include "tempest/models/LlamaCli.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>

namespace tempest::models {

static std::string shell_escape_single_quotes(const std::string& s) {
  // Wrap in single quotes and escape internal single quotes:  ' -> '\''  (POSIX sh)
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('\'');
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

LlamaRunResult run_llama_cli(const LlamaRunConfig& cfg) {
  LlamaRunResult r;

  if (cfg.runner_path.empty()) {
    r.exit_code = 127;
    r.stderr_text = "llama runner path not set (build may not have produced llama-simple target)";
    return r;
  }
  if (cfg.model_path.empty()) {
    r.exit_code = 2;
    r.stderr_text = "model path is empty";
    return r;
  }

  // Note: We call llama.cpp's `llama-simple` example binary to get real output quickly.
  // Later we can swap to linking llama.cpp directly for lower latency.
  std::ostringstream cmd;
  cmd << shell_escape_single_quotes(cfg.runner_path)
      << " -m " << shell_escape_single_quotes(cfg.model_path)
      << " -n " << cfg.max_tokens
      << " 2>&1";
  // Prompt for llama-simple is positional (not -p).
  if (!cfg.prompt.empty()) {
    cmd << " " << shell_escape_single_quotes(cfg.prompt);
  }

  FILE* pipe = popen(cmd.str().c_str(), "r");
  if (!pipe) {
    r.exit_code = 127;
    r.stderr_text = std::string("failed to spawn llama-cli: ") + ::strerror(errno);
    return r;
  }

  std::array<char, 4096> buf{};
  while (true) {
    const size_t n = fread(buf.data(), 1, buf.size(), pipe);
    if (n > 0) r.stdout_text.append(buf.data(), n);
    if (n < buf.size()) {
      if (feof(pipe)) break;
      if (ferror(pipe)) break;
    }
  }

  const int code = pclose(pipe);
  r.exit_code = code;

  // Heuristic cleanup: llama-simple prints a lot of diagnostics.
  // Try to return only the generated tail (starting from the prompt if present).
  if (!cfg.prompt.empty()) {
    const size_t p = r.stdout_text.rfind(cfg.prompt);
    if (p != std::string::npos) {
      r.stdout_text = r.stdout_text.substr(p);
    }
  }
  return r;
}

} // namespace tempest::models

