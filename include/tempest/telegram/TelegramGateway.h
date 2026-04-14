#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tempest::telegram {

struct TelegramMessage {
  long long update_id{0};
  long long chat_id{0};
  std::string text;
};

class TelegramGateway {
public:
  TelegramGateway() = default;
  ~TelegramGateway();

  bool available() const;
  std::string status_line() const;

  // Configuration is read from environment by default:
  // - TEMPEST_TELEGRAM_TOKEN (required to run)
  // - TEMPEST_TELEGRAM_ALLOW_CHATS (optional comma-separated chat ids)
  // If env vars are not set, also reads `tempest.secrets` from the current working directory.
  void configure_from_env();

  bool start(const std::function<void(const TelegramMessage&)>& on_message, std::string* err);
  void stop();

  bool send_message(long long chat_id, const std::string& text, std::string* err) const;

private:
  bool allowed_chat(long long chat_id) const;
  void loop();

  std::string token_;
  std::vector<long long> allow_chats_;

  std::function<void(const TelegramMessage&)> on_message_;
  mutable std::mutex mu_;
  std::atomic<bool> running_{false};
  std::thread th_;
  long long last_update_id_{0};
};

} // namespace tempest::telegram

