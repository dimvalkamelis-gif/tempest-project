#pragma once

#include <string>
#include <vector>

namespace tempest::store {

struct StoredFile {
  long long id{0};
  std::string path;
  long long mtime_unix{0};
  long long size_bytes{0};
  std::string sha256_hex;
};

struct StoredChunk {
  long long id{0};
  long long file_id{0};
  int chunk_index{0};
  long long offset_start{0};
  long long offset_end{0};
  std::string text;
};

class SqliteStore {
public:
  explicit SqliteStore(std::string db_path);
  bool open_or_create(std::string* err);

  bool upsert_file(const StoredFile& f, long long* out_file_id, std::string* err);
  bool replace_chunks(long long file_id, const std::vector<StoredChunk>& chunks, std::string* err);

  bool file_needs_reindex(const std::string& path, long long mtime_unix, long long size_bytes, bool* out_needs, std::string* err);

private:
  std::string db_path_;
  void* db_{nullptr}; // sqlite3*
};

} // namespace tempest::store

