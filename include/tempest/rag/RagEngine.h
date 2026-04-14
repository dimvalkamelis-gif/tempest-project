#pragma once

#include <string>
#include <vector>

namespace tempest::rag {

struct RetrievedChunk {
  std::string source;
  std::string title;
  std::string text;
  double score{0.0};
};

struct RagResult {
  std::vector<RetrievedChunk> chunks;
  std::string synthesized_context;
};

class RagEngine {
public:
  RagEngine() = default;

  bool available() const;
  void add_fallback_chunk_dir(const std::string& dir);
  void set_wiki_index_dir(const std::string& dir);
  RagResult retrieve(const std::string& query, int top_k) const;

private:
  std::vector<std::string> fallback_dirs_;
  std::string wiki_dir_;
};

} // namespace tempest::rag

