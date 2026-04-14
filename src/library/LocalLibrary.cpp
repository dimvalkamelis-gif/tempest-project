#include "tempest/library/LocalLibrary.h"

#include "tempest/store/SqliteStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace tempest::library {

LocalLibrary::LocalLibrary(std::string db_path)
    : db_path_(std::move(db_path)),
      store_(std::make_unique<tempest::store::SqliteStore>(db_path_)) {}

LocalLibrary::~LocalLibrary() {
  unwatch_all();
}

std::vector<std::pair<long long,long long>> LocalLibrary::chunk_ranges(long long size_bytes, long long chunk_bytes, long long overlap_bytes) {
  std::vector<std::pair<long long,long long>> out;
  if (size_bytes <= 0) return out;
  if (chunk_bytes <= 0) chunk_bytes = 4096;
  if (overlap_bytes < 0) overlap_bytes = 0;
  if (overlap_bytes >= chunk_bytes) overlap_bytes = chunk_bytes / 4;

  long long start = 0;
  while (start < size_bytes) {
    long long end = start + chunk_bytes;
    if (end > size_bytes) end = size_bytes;
    out.emplace_back(start, end);
    if (end == size_bytes) break;
    start = end - overlap_bytes;
    if (start < 0) start = 0;
  }
  return out;
}

static bool is_txt_file(const fs::path& p) {
  if (!fs::is_regular_file(p)) return false;
  auto ext = p.extension().string();
  for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return ext == ".txt";
}

static long long file_mtime_unix(const fs::path& p) {
  const auto ftime = fs::last_write_time(p);
  // Convert file_time_type -> system_clock time_point (C++17-friendly-ish approximation)
  const auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
      ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  return static_cast<long long>(sctp.time_since_epoch().count());
}

static std::string hex_u64(uint64_t v) {
  static const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[i] = digits[v & 0xfu];
    v >>= 4u;
  }
  return out;
}

static bool write_fallback_index(const std::string& file_path,
                                 const std::vector<tempest::store::StoredChunk>& chunks,
                                 std::string* err) {
  try {
    fs::path base = fs::path(".tempest_local_index");
    fs::create_directories(base / "chunks");

    const uint64_t key = static_cast<uint64_t>(std::hash<std::string>{}(file_path));
    fs::path outp = base / "chunks" / (hex_u64(key) + ".txt");

    std::ofstream out(outp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (err) *err = "failed to write fallback index file: " + outp.string();
      return false;
    }

    // Very simple format: header + chunks separated by delimiter.
    out << "path: " << file_path << "\n";
    out << "chunks: " << chunks.size() << "\n";
    out << "----\n";
    for (const auto& c : chunks) {
      out << "[chunk " << c.chunk_index << " bytes " << c.offset_start << "-" << c.offset_end << "]\n";
      out << c.text << "\n";
      out << "----\n";
    }
    if (err) err->clear();
    return true;
  } catch (const std::exception& ex) {
    if (err) *err = std::string("fallback index error: ") + ex.what();
    return false;
  }
}

bool LocalLibrary::index_file_if_needed(const std::string& file_path, IndexStats* stats, std::string* err) {
  fs::path p(file_path);
  if (!fs::exists(p) || !fs::is_regular_file(p)) {
    if (err) *err = "not a regular file: " + file_path;
    return false;
  }
  if (!is_txt_file(p)) return true; // ignore silently

  const long long sz = static_cast<long long>(fs::file_size(p));
  const long long mt = file_mtime_unix(p);

  bool use_sqlite = false;
  long long file_id = 0;
  {
    std::string e;
    bool needs = true;
    if (store_->open_or_create(&e)) {
      use_sqlite = true;
      if (!store_->file_needs_reindex(p.string(), mt, sz, &needs, &e)) {
        if (err) *err = e;
        return false;
      }
      if (!needs) return true;

      tempest::store::StoredFile f;
      f.path = p.string();
      f.mtime_unix = mt;
      f.size_bytes = sz;
      if (!store_->upsert_file(f, &file_id, &e)) {
        // If sqlite is present but fails, fall back to file index.
        use_sqlite = false;
      }
    }
  }

  std::ifstream in(p, std::ios::binary);
  if (!in) {
    if (err) *err = "failed to open: " + p.string();
    return false;
  }
  std::string content;
  content.resize(static_cast<size_t>(sz));
  if (sz > 0) in.read(&content[0], sz);

  const auto ranges = chunk_ranges(sz, 8000 /*bytes*/, 800 /*overlap*/);
  std::vector<tempest::store::StoredChunk> chunks;
  chunks.reserve(ranges.size());
  int idx = 0;
  for (auto [a, b] : ranges) {
    tempest::store::StoredChunk c;
    c.file_id = file_id; // may be 0 in fallback mode
    c.chunk_index = idx++;
    c.offset_start = a;
    c.offset_end = b;
    c.text = content.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
    chunks.push_back(std::move(c));
  }

  if (use_sqlite) {
    std::string e;
    if (!store_->replace_chunks(file_id, chunks, &e)) {
      // Fall back to file index on any sqlite error.
      use_sqlite = false;
    }
  }
  if (!use_sqlite) {
    std::string e;
    if (!write_fallback_index(p.string(), chunks, &e)) {
      if (err) *err = e;
      return false;
    }
  }

  if (stats) {
    stats->files_indexed += 1;
    stats->chunks_written += static_cast<int>(chunks.size());
  }
  if (err) err->clear();
  return true;
}

bool LocalLibrary::add_path(const std::string& path, IndexStats* stats, std::string* err) {
  fs::path p(path);
  if (!fs::exists(p)) {
    if (err) *err = "path does not exist: " + path;
    return false;
  }

  if (fs::is_regular_file(p)) {
    return index_file_if_needed(p.string(), stats, err);
  }

  if (fs::is_directory(p)) {
    std::string e;
    for (auto const& entry : fs::recursive_directory_iterator(p)) {
      if (!entry.is_regular_file()) continue;
      (void)index_file_if_needed(entry.path().string(), stats, &e);
    }
    if (err) err->clear();
    return true;
  }

  if (err) *err = "unsupported path type: " + path;
  return false;
}

bool LocalLibrary::watch_dir(const std::string& dir, std::string* err) {
  fs::path p(dir);
  if (!fs::exists(p) || !fs::is_directory(p)) {
    if (err) *err = "not a directory: " + dir;
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& w : watched_) {
      if (w == p.string()) {
        if (err) err->clear();
        return true;
      }
    }
    watched_.push_back(p.string());
  }

  if (!watcher_.joinable()) {
    stop_ = false;
    watcher_ = std::thread([this] { watch_loop(); });
  }

  if (err) err->clear();
  return true;
}

void LocalLibrary::unwatch_all() {
  stop_ = true;
  if (watcher_.joinable()) watcher_.join();
  std::lock_guard<std::mutex> lk(mu_);
  watched_.clear();
}

std::vector<std::string> LocalLibrary::watched_dirs() const {
  std::lock_guard<std::mutex> lk(mu_);
  return watched_;
}

void LocalLibrary::watch_loop() {
  while (!stop_) {
    std::vector<std::string> dirs;
    {
      std::lock_guard<std::mutex> lk(mu_);
      dirs = watched_;
    }

    for (const auto& d : dirs) {
      IndexStats st;
      std::string err;
      (void)add_path(d, &st, &err);
    }

    for (int i = 0; i < 10 && !stop_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }
}

} // namespace tempest::library

