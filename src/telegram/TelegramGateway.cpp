#include "tempest/telegram/TelegramGateway.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tempest::telegram {

bool telegram_get_updates(const std::string& token, long long offset, std::string* out_body, std::string* err);
std::vector<TelegramMessage> telegram_parse_updates(const std::string& body);
bool telegram_send_message(const std::string& token, long long chat_id, const std::string& text, std::string* err);

static std::vector<long long> parse_allow_chats(const char* s) {
  std::vector<long long> out;
  if (!s) return out;
  std::string cur;
  for (const char* p = s; ; ++p) {
    const char c = *p;
    if (c == ',' || c == '\0') {
      if (!cur.empty()) {
        try { out.push_back(std::stoll(cur)); } catch (...) {}
        cur.clear();
      }
      if (c == '\0') break;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n') continue;
    cur.push_back(c);
  }
  return out;
}

TelegramGateway::~TelegramGateway() {
  stop();
}

bool TelegramGateway::available() const {
#if defined(TEMPEST_ENABLE_TELEGRAM) && TEMPEST_ENABLE_TELEGRAM
  return true;
#else
  return false;
#endif
}

std::string TelegramGateway::status_line() const {
  if (!available()) return "telegram:disabled";
  std::ostringstream oss;
  oss << "telegram:" << (running_.load() ? "on" : "off");
  if (running_.load()) oss << " last_update=" << last_update_id_;
  return oss.str();
}

void TelegramGateway::configure_from_env() {
  const char* tok = std::getenv("TEMPEST_TELEGRAM_TOKEN");
  if (tok) token_ = tok;
  const char* allow = std::getenv("TEMPEST_TELEGRAM_ALLOW_CHATS");
  if (allow) allow_chats_ = parse_allow_chats(allow);

  // Fallback: read from local secrets file if env isn't set.
  if (token_.empty() || allow_chats_.empty()) {
    std::ifstream in("tempest.secrets");
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      if (line[0] == '#') continue;
      const auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = line.substr(0, eq);
      const std::string val = line.substr(eq + 1);
      if (token_.empty() && key == "TEMPEST_TELEGRAM_TOKEN") token_ = val;
      if (allow_chats_.empty() && key == "TEMPEST_TELEGRAM_ALLOW_CHATS") allow_chats_ = parse_allow_chats(val.c_str());
    }
  }
}

bool TelegramGateway::allowed_chat(long long chat_id) const {
  if (allow_chats_.empty()) return true; // pairing mode: allow first contact
  for (auto id : allow_chats_) if (id == chat_id) return true;
  return false;
}

bool TelegramGateway::start(const std::function<void(const TelegramMessage&)>& on_message, std::string* err) {
  if (!available()) {
    if (err) *err = "telegram not built";
    return false;
  }
  configure_from_env();
  if (token_.empty()) {
    if (err) *err = "missing TEMPEST_TELEGRAM_TOKEN";
    return false;
  }
  if (running_.exchange(true)) {
    if (err) err->clear();
    return true;
  }
  on_message_ = on_message;
  th_ = std::thread([this] { loop(); });
  if (err) err->clear();
  return true;
}

void TelegramGateway::stop() {
  if (!running_.exchange(false)) return;
  if (th_.joinable()) th_.join();
}

bool TelegramGateway::send_message(long long chat_id, const std::string& text, std::string* err) const {
  if (token_.empty()) {
    if (err) *err = "missing TEMPEST_TELEGRAM_TOKEN";
    return false;
  }
  if (!allowed_chat(chat_id)) {
    if (err) *err = "chat id not allowed";
    return false;
  }
  return telegram_send_message(token_, chat_id, text, err);
}

void TelegramGateway::loop() {
  long long offset = 0;
  while (running_.load()) {
    std::string body, e;
    if (!telegram_get_updates(token_, offset, &body, &e)) {
      // brief backoff
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }
    const auto msgs = telegram_parse_updates(body);
    for (const auto& m : msgs) {
      last_update_id_ = m.update_id;
      offset = m.update_id + 1;
      // Pairing: if no allowlist set, lock to first chat id we see.
      if (allow_chats_.empty()) {
        allow_chats_.push_back(m.chat_id);
      }
      if (!allowed_chat(m.chat_id)) continue;
      if (on_message_) on_message_(m);
    }
  }
}

} // namespace tempest::telegram