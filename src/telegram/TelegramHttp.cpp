#include "tempest/telegram/TelegramGateway.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace tempest::telegram {

static std::string sh_quote(const std::string& s) {
  // POSIX single-quote escaping.
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('\'');
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

static bool run_curl(const std::string& cmd, std::string* out, std::string* err) {
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    if (err) *err = std::string("failed to spawn curl: ") + ::strerror(errno);
    return false;
  }
  std::array<char, 4096> buf{};
  std::string text;
  while (true) {
    const size_t n = fread(buf.data(), 1, buf.size(), pipe);
    if (n > 0) text.append(buf.data(), n);
    if (n < buf.size()) {
      if (feof(pipe)) break;
      if (ferror(pipe)) break;
    }
  }
  const int code = pclose(pipe);
  if (code != 0) {
    if (err) *err = "curl exited with code " + std::to_string(code);
    if (out) *out = text;
    return false;
  }
  if (out) *out = text;
  if (err) err->clear();
  return true;
}

// Very small JSON helpers (Telegram responses are predictable enough).
static bool json_find_number(const std::string& s, const std::string& key, size_t from, long long* out, size_t* out_pos) {
  const std::string needle = "\"" + key + "\":";
  const size_t k = s.find(needle, from);
  if (k == std::string::npos) return false;
  size_t i = k + needle.size();
  while (i < s.size() && (s[i] == ' ')) i++;
  bool neg = false;
  if (i < s.size() && s[i] == '-') { neg = true; i++; }
  long long v = 0;
  bool any = false;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    any = true;
    v = v * 10 + (s[i] - '0');
    i++;
  }
  if (!any) return false;
  if (neg) v = -v;
  if (out) *out = v;
  if (out_pos) *out_pos = i;
  return true;
}

static bool json_find_string(const std::string& s, const std::string& key, size_t from, std::string* out, size_t* out_pos) {
  const std::string needle = "\"" + key + "\":\"";
  const size_t k = s.find(needle, from);
  if (k == std::string::npos) return false;
  size_t i = k + needle.size();
  std::string v;
  while (i < s.size()) {
    char c = s[i++];
    if (c == '\\') {
      if (i >= s.size()) break;
      char e = s[i++];
      if (e == 'n') v.push_back('\n');
      else if (e == 't') v.push_back('\t');
      else v.push_back(e);
      continue;
    }
    if (c == '"') break;
    v.push_back(c);
  }
  if (out) *out = v;
  if (out_pos) *out_pos = i;
  return true;
}

static std::vector<TelegramMessage> parse_updates(const std::string& body) {
  std::vector<TelegramMessage> out;
  size_t pos = 0;
  while (true) {
    long long update_id = 0;
    size_t p2 = 0;
    if (!json_find_number(body, "update_id", pos, &update_id, &p2)) break;

    // Restrict parsing to near this update.
    size_t scope_start = (p2 > 2000) ? (p2 - 2000) : 0;
    size_t scope_end = std::min(body.size(), p2 + 2000);
    const std::string scope = body.substr(scope_start, scope_end - scope_start);

    long long chat_id = 0;
    std::string text;
    (void)json_find_number(scope, "id", 0, &chat_id, nullptr);      // chat.id is typically the first "id" in scope
    (void)json_find_string(scope, "text", 0, &text, nullptr);

    TelegramMessage m;
    m.update_id = update_id;
    m.chat_id = chat_id;
    m.text = text;
    out.push_back(std::move(m));

    pos = p2;
  }
  return out;
}

bool telegram_get_updates(const std::string& token, long long offset, std::string* out_body, std::string* err) {
  std::ostringstream url;
  url << "https://api.telegram.org/bot" << token
      << "/getUpdates?timeout=25&allowed_updates=%5B%22message%22%5D";
  if (offset > 0) url << "&offset=" << offset;

  std::ostringstream cmd;
  cmd << "curl -sS " << sh_quote(url.str());
  return run_curl(cmd.str(), out_body, err);
}

std::vector<TelegramMessage> telegram_parse_updates(const std::string& body) {
  return parse_updates(body);
}

bool telegram_send_message(const std::string& token, long long chat_id, const std::string& text, std::string* err) {
  std::ostringstream url;
  url << "https://api.telegram.org/bot" << token << "/sendMessage";

  // Use application/x-www-form-urlencoded via --data-urlencode to safely transmit text.
  std::ostringstream cmd;
  cmd << "curl -sS -X POST " << sh_quote(url.str())
      << " --data-urlencode " << sh_quote("chat_id=" + std::to_string(chat_id))
      << " --data-urlencode " << sh_quote("text=" + text);

  std::string body;
  return run_curl(cmd.str(), &body, err);
}

} // namespace tempest::telegram

