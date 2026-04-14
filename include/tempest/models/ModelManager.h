#pragma once

#include <functional>
#include <optional>
#include <string>

namespace tempest::models {

enum class ModelId {
  OneBit,
  Tiny08B,
  Reasoning3B,
};

struct ModelStatus {
  ModelId active;
  std::string active_name;
  bool llama_enabled;
};

class ModelManager {
public:
  ModelManager();

  ModelStatus status() const;
  void switch_model();
  std::string generate(const std::string& user_text, std::function<void(const std::string&)> on_token = nullptr) const;

  void set_model_path(ModelId id, const std::string& path);
  void set_auto_routing(bool enabled) { auto_routing_ = enabled; }
  bool auto_routing() const { return auto_routing_; }

private:
  ModelId route(const std::string& user_text) const;

  ModelId active_{ModelId::Tiny08B};
  bool auto_routing_{true};
  std::string model_path_1bit_;
  std::string model_path_08b_;
  std::string model_path_3b_;
};

std::string to_string(ModelId id);

} // namespace tempest::models

