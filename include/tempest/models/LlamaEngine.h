#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct llama_model;
struct llama_context;
struct llama_vocab;

namespace tempest::models {

struct LlamaEngineConfig {
  std::string model_path;
  int n_ctx{2048};
  int n_threads{0};        // 0 = auto
  int n_threads_batch{0};  // 0 = auto

  float temp{0.7f};
  int top_k{40};
  float top_p{0.95f};
};

class LlamaEngine {
public:
  LlamaEngine();
  ~LlamaEngine();

  bool load(const LlamaEngineConfig& cfg, std::string* err);
  bool loaded() const;
  std::string model_path() const { return model_path_; }

  // Generates up to max_new_tokens. Greedy sampling for stability.
  bool generate(const std::string& prompt, int max_new_tokens, std::string* out_text, std::string* err, std::function<void(const std::string&)> on_token = nullptr);

private:
  void unload();
  static void ensure_backend_init();

  mutable std::mutex mu_;
  std::string model_path_;
  LlamaEngineConfig config_;
  llama_model* model_{nullptr};
  llama_context* ctx_{nullptr};
  const llama_vocab* vocab_{nullptr};
  bool backend_inited_{false};
};

} // namespace tempest::models

