#include "tempest/models/LlamaEngine.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA
#  include "llama.h"
#endif

namespace tempest::models {

LlamaEngine::LlamaEngine() = default;

LlamaEngine::~LlamaEngine() {
  std::lock_guard<std::mutex> lk(mu_);
  unload();
}

void LlamaEngine::ensure_backend_init() {
#if defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA
  static std::once_flag once;
  std::call_once(once, [] {
    // Silence llama.cpp/ggml logs for a clean UX.
    llama_log_set(
        [](enum ggml_log_level /*level*/, const char* /*text*/, void* /*user_data*/) {},
        nullptr);
    llama_backend_init();
  });
#endif
}

bool LlamaEngine::loaded() const {
  std::lock_guard<std::mutex> lk(mu_);
  return model_ != nullptr && ctx_ != nullptr && vocab_ != nullptr;
}

void LlamaEngine::unload() {
#if defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA
  if (ctx_) {
    llama_free(ctx_);
    ctx_ = nullptr;
  }
  if (model_) {
    llama_model_free(model_);
    model_ = nullptr;
  }
  vocab_ = nullptr;
#endif
  model_path_.clear();
}

bool LlamaEngine::load(const LlamaEngineConfig& cfg, std::string* err) {
#if !(defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA)
  (void)cfg;
  if (err) *err = "llama support not compiled";
  return false;
#else
  std::lock_guard<std::mutex> lk(mu_);
  ensure_backend_init();

  if (cfg.model_path.empty()) {
    if (err) *err = "model_path is empty";
    return false;
  }

  if (model_ && model_path_ == cfg.model_path) {
    if (err) err->clear();
    return true;
  }

  unload();

  llama_model_params mp = llama_model_default_params();
  // Prefer mmap for speed if supported.
  mp.use_mmap  = llama_supports_mmap();
  mp.use_mlock = false;

  model_ = llama_model_load_from_file(cfg.model_path.c_str(), mp);
  if (!model_) {
    if (err) *err = "failed to load model: " + cfg.model_path;
    return false;
  }
  vocab_ = llama_model_get_vocab(model_);

  llama_context_params cp = llama_context_default_params();
  cp.n_ctx = cfg.n_ctx;
  cp.n_batch = cfg.n_ctx;
  cp.n_ubatch = 512;
  cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;

  ctx_ = llama_init_from_model(model_, cp);
  if (!ctx_) {
    if (err) *err = "failed to create llama context";
    unload();
    return false;
  }

  const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  const int nt  = cfg.n_threads > 0 ? cfg.n_threads : std::min(8, hw);
  const int ntb = cfg.n_threads_batch > 0 ? cfg.n_threads_batch : std::min(8, hw);
  llama_set_n_threads(ctx_, nt, ntb);

  config_ = cfg;
  model_path_ = cfg.model_path;
  if (err) err->clear();
  return true;
#endif
}

static std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
  // First call with NULL to get size (negative means required).
  const int32_t nmax = static_cast<int32_t>(text.size() + 8);
  std::vector<llama_token> toks;
  toks.resize(static_cast<size_t>(nmax));
  const int32_t n = llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()),
                                   toks.data(), nmax, true, true);
  if (n < 0) {
    toks.clear();
    return toks;
  }
  toks.resize(static_cast<size_t>(n));
  return toks;
}

static std::string token_to_piece(const llama_vocab* vocab, llama_token t) {
  char buf[256];
  const int n = llama_token_to_piece(vocab, t, buf, sizeof(buf), 0, true);
  if (n <= 0) return {};
  return std::string(buf, buf + n);
}

bool LlamaEngine::generate(const std::string& prompt, int max_new_tokens, std::string* out_text, std::string* err, std::function<void(const std::string&)> on_token) {
#if !(defined(TEMPEST_ENABLE_LLAMA) && TEMPEST_ENABLE_LLAMA)
  (void)prompt; (void)max_new_tokens;
  if (err) *err = "llama support not compiled";
  return false;
#else
  std::lock_guard<std::mutex> lk(mu_);
  if (!ctx_ || !model_ || !vocab_) {
    if (err) *err = "model not loaded";
    return false;
  }
  if (max_new_tokens <= 0) max_new_tokens = 32;

  // Clear KV/memory between generations for stability.
  llama_memory_clear(llama_get_memory(ctx_), true);

  // Wrap in prompt template if it's a chat model.
  std::string formatted_prompt = prompt;
  const char* tmpl = llama_model_chat_template(model_, nullptr);
  if (tmpl) {
    llama_chat_message msgs[2];
    msgs[0] = {"system", "You are Tempest, a helpful and accurate AI assistant. Answer the user's questions clearly and concisely based on provided context if any. If you don't know the answer, say so; do not hallucinate."};
    msgs[1] = {"user", prompt.c_str()};
    
    int32_t len = llama_chat_apply_template(tmpl, msgs, 2, true, nullptr, 0);
    if (len > 0) {
      std::vector<char> buf(static_cast<size_t>(len) + 1);
      if (llama_chat_apply_template(tmpl, msgs, 2, true, buf.data(), static_cast<int32_t>(buf.size())) > 0) {
        formatted_prompt = std::string(buf.data());
      }
    }
  }

  auto toks = tokenize(vocab_, formatted_prompt);
  if (toks.empty()) {
    if (err) *err = "tokenize failed";
    return false;
  }

  // Safety: truncate prompt if it exceeds context limit.
  if (static_cast<int>(toks.size()) > config_.n_ctx - 4) {
    toks.resize(static_cast<size_t>(config_.n_ctx - 4));
  }

  // Setup sampler chain for high-quality generation.
  auto sparams = llama_sampler_chain_default_params();
  llama_sampler* smpl = llama_sampler_chain_init(sparams);
  llama_sampler_chain_add(smpl, llama_sampler_init_top_k(config_.top_k));
  llama_sampler_chain_add(smpl, llama_sampler_init_top_p(config_.top_p, 1));
  llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.1f)); // Lower temp for factual accuracy
  llama_sampler_chain_add(smpl, llama_sampler_init_dist(static_cast<uint32_t>(time(nullptr))));

  // Evaluate prompt
  llama_batch batch = llama_batch_get_one(toks.data(), static_cast<int32_t>(toks.size()));
  int decode_res = llama_decode(ctx_, batch);
  if (decode_res != 0) {
    llama_sampler_free(smpl);
    if (err) *err = "llama_decode failed on prompt (code " + std::to_string(decode_res) + ")";
    return false;
  }

  std::string gen;
  gen.reserve(1024);

  for (int i = 0; i < max_new_tokens; ++i) {
    const llama_token next = llama_sampler_sample(smpl, ctx_, -1);
    llama_sampler_accept(smpl, next);

    if (llama_vocab_is_eog(vocab_, next)) break;

    std::string piece = token_to_piece(vocab_, next);
    
    // Safety: don't let the model generate the ChatML stop tags manually if they leak.
    if (piece.find("<|im_end|>") != std::string::npos) break;

    gen += piece;
    if (on_token) on_token(piece);

    llama_token tok_arr[1] = { next };
    llama_batch b2 = llama_batch_get_one(tok_arr, 1);
    if (llama_decode(ctx_, b2) != 0) break;
  }

  llama_sampler_free(smpl);

  if (out_text) *out_text = gen;
  if (err) err->clear();
  return true;
#endif
}

} // namespace tempest::models

