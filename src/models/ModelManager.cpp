#include "tempest/models/ModelManager.h"

#include "tempest/models/LlamaCli.h"
#include "tempest/models/LlamaEngine.h"

#include "tempest/core/Strings.h"
#include <filesystem>
#include <functional>

namespace tempest::models {

static LlamaEngine& engine_singleton() {
  static LlamaEngine e;
  return e;
}

static std::string default_model_path(ModelId id) {
  // Repo-relative defaults matching your workspace layout.
  switch (id) {
    case ModelId::OneBit:
      return "1bit_model/model.gguf";
    case ModelId::Tiny08B:
      return "0.8b_model/model.gguf";
    case ModelId::Reasoning3B:
      return "3b_model/model.gguf";
  }
  return {};
}

ModelManager::ModelManager() {
  model_path_1bit_ = default_model_path(ModelId::OneBit);
  model_path_08b_ = default_model_path(ModelId::Tiny08B);
  model_path_3b_ = default_model_path(ModelId::Reasoning3B);
}

ModelStatus ModelManager::status() const {
  ModelStatus s;
  s.active = active_;
  s.active_name = to_string(active_);
#if defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA
  s.llama_enabled = true;
#else
  s.llama_enabled = false;
#endif
  return s;
}

void ModelManager::switch_model() {
  // Manual cycle (auto routing may ignore this).
  if (active_ == ModelId::OneBit) active_ = ModelId::Tiny08B;
  else if (active_ == ModelId::Tiny08B) active_ = ModelId::Reasoning3B;
  else active_ = ModelId::OneBit;
}

void ModelManager::set_model_path(ModelId id, const std::string& path) {
  switch (id) {
    case ModelId::OneBit: model_path_1bit_ = path; return;
    case ModelId::Tiny08B: model_path_08b_ = path; return;
    case ModelId::Reasoning3B: model_path_3b_ = path; return;
  }
}

ModelId ModelManager::route(const std::string& user_text) const {
  // Simple heuristic router:
  // - very short prompts -> 1bit if present
  // - "why/how/prove/derive/plan" -> 3b
  // - otherwise -> 0.8b
  const auto n = user_text.size();
  auto has = [&](const char* needle) {
    return user_text.find(needle) != std::string::npos;
  };
  
  auto path_valid = [](const std::string& p) {
    return !p.empty() && std::filesystem::exists(p);
  };

  if (n < 40 && path_valid(model_path_1bit_)) return ModelId::OneBit;
  if (has("why") || has("how") || has("prove") || has("derive") || has("plan") || has("step")) {
    if (path_valid(model_path_3b_)) return ModelId::Reasoning3B;
  }
  return ModelId::Tiny08B;
}

std::string ModelManager::generate(const std::string& user_text, std::function<void(const std::string&)> on_token) const {
#if defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA
  const ModelId chosen = auto_routing_ ? route(user_text) : active_;
  std::string model_path;
  if (chosen == ModelId::OneBit) model_path = model_path_1bit_;
  else if (chosen == ModelId::Reasoning3B) model_path = model_path_3b_;
  else model_path = model_path_08b_;
  if (!std::filesystem::exists(model_path)) {
    return "model file not found: " + model_path;
  }

  // Fast path: run in-process via llama.cpp API.
  {
    auto& eng = engine_singleton();
    LlamaEngineConfig ecfg;
    ecfg.model_path = model_path;
    ecfg.n_ctx = 2048;
    std::string e;
    if (!eng.load(ecfg, &e)) {
      // fallback to runner if available
    } else {
      std::string out;
      
      // Adaptive token limit based on heuristic.
      auto has = [&](const std::string& n) {
        return user_text.find(n) != std::string::npos || user_text.find(tempest::core::to_lower(n)) != std::string::npos;
      };
      
      int limit = 128;
      if (user_text.size() > 120 || has("write") || has("explain") || has("tell") || has("essay") || has("story") || has("code") || has("how")) {
        limit = 2048;
      }

      if (eng.generate(user_text, limit, &out, &e, on_token)) {
        if (out.empty()) return "(no output)";
        return out;
      }
      // fall back below
    }
  }

  // Fallback path: spawn llama-simple runner if linked build produced it.
  LlamaRunConfig cfg;
#ifdef TEMPEST_LLAMA_RUNNER_PATH
  cfg.runner_path = TEMPEST_LLAMA_RUNNER_PATH;
#endif
  cfg.model_path = model_path;
  cfg.prompt = user_text;
  cfg.max_tokens = 32;
  cfg.temperature = 0.7f;
  const auto res = run_llama_cli(cfg);
  if (res.exit_code != 0) {
    return "llama runner failed (code " + std::to_string(res.exit_code) + "):\n" + res.stdout_text + res.stderr_text;
  }
  return res.stdout_text;
#else
  const ModelId chosen = auto_routing_ ? route(user_text) : active_;
  (void)chosen;
  return std::string("llama.cpp not enabled in this build. (auto=") + (auto_routing_ ? "on" : "off") +
         ", model=" + to_string(chosen) + ")";
#endif
}

std::string to_string(ModelId id) {
  switch (id) {
    case ModelId::OneBit:
      return "1bit";
    case ModelId::Tiny08B:
      return "0.8b";
    case ModelId::Reasoning3B:
      return "3b";
  }
  return "unknown";
}

} // namespace tempest::models

