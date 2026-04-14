#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tempest::store { class SqliteStore; }

namespace tempest::library {

struct IndexStats {
  int files_indexed{0};
  int chunks_written{0};
};

class LocalLibrary {
public:
  explicit LocalLibrary(std::string db_path);
  ~LocalLibrary();

  // Index a file or directory (recursively adds *.txt).
  bool add_path(const std::string& path, IndexStats* stats, std::string* err);

  // Watch a directory using polling (portable; no inotify dependency).
  bool watch_dir(const std::string& dir, std::string* err);
  void unwatch_all();

  std::vector<std::string> watched_dirs() const;

private:
  bool index_file_if_needed(const std::string& file_path, IndexStats* stats, std::string* err);
  static std::vector<std::pair<long long,long long>> chunk_ranges(long long size_bytes, long long chunk_bytes, long long overlap_bytes);

  void watch_loop();

  std::string db_path_;
  std::unique_ptr<tempest::store::SqliteStore> store_;

  mutable std::mutex mu_;
  std::vector<std::string> watched_;
  std::atomic<bool> stop_{false};
  std::thread watcher_;
};

} // namespace tempest::library

