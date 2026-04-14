#include "tempest/wiki/WikiIngestor.h"

#include "tempest/core/Strings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace tempest::wiki {

static uint64_t fnv1a64(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

static std::vector<std::string> tokenize(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  cur.reserve(32);
  for (char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c)) {
      cur.push_back(static_cast<char>(std::tolower(c)));
    } else {
      if (cur.size() >= 2) out.push_back(cur);
      cur.clear();
    }
  }
  if (cur.size() >= 2) out.push_back(cur);
  return out;
}

static std::vector<std::pair<int64_t,int64_t>> chunk_ranges(int64_t size_bytes, int64_t chunk_bytes, int64_t overlap_bytes) {
  std::vector<std::pair<int64_t,int64_t>> out;
  if (size_bytes <= 0) return out;
  if (chunk_bytes <= 0) chunk_bytes = 4096;
  if (overlap_bytes < 0) overlap_bytes = 0;
  if (overlap_bytes >= chunk_bytes) overlap_bytes = chunk_bytes / 4;

  int64_t start = 0;
  while (start < size_bytes) {
    int64_t end = start + chunk_bytes;
    if (end > size_bytes) end = size_bytes;
    out.emplace_back(start, end);
    if (end == size_bytes) break;
    start = end - overlap_bytes;
    if (start < 0) start = 0;
  }
  return out;
}

static std::string strip_simple_markup(std::string s) {
  // Very lightweight cleanup: drop ref tags, collapse brackets.
  auto erase_all = [&](const std::string& needle) {
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) s.erase(pos, needle.size());
  };
  erase_all("\r");

  // Remove <ref ...>...</ref> blocks (naive).
  while (true) {
    const size_t a = s.find("<ref");
    if (a == std::string::npos) break;
    const size_t b = s.find("</ref>", a);
    if (b == std::string::npos) break;
    s.erase(a, (b + 6) - a);
  }
  erase_all("<ref/>");

  // Replace wiki links [[A|B]] -> B, [[A]] -> A
  while (true) {
    const size_t a = s.find("[[");
    if (a == std::string::npos) break;
    const size_t b = s.find("]]", a);
    if (b == std::string::npos) break;
    const std::string inner = s.substr(a + 2, b - (a + 2));
    const size_t bar = inner.find('|');
    const std::string rep = (bar == std::string::npos) ? inner : inner.substr(bar + 1);
    s.replace(a, (b + 2) - a, rep);
  }

  // Drop templates {{...}} (naive, non-nested).
  while (true) {
    const size_t a = s.find("{{");
    if (a == std::string::npos) break;
    const size_t b = s.find("}}", a);
    if (b == std::string::npos) break;
    s.erase(a, (b + 2) - a);
  }

  return s;
}

struct ShardWriter {
  fs::path dir;
  int shard_target_mb{64};
  int shard_index{0};
  std::ofstream out;
  int64_t bytes_in_shard{0};
  int current_shard_id{-1};

  fs::path open_next(std::string* err) {
    if (out.is_open()) out.close();
    bytes_in_shard = 0;
    current_shard_id = shard_index;
    const std::ostringstream name = [&]{
      std::ostringstream oss;
      oss << "shard_" << std::setw(5) << std::setfill('0') << shard_index++ << ".txt";
      return oss;
    }();
    fs::path p = dir / name.str();
    out.open(p, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (err) *err = "failed to open shard: " + p.string();
    }
    return p;
  }
};

struct ChunkRec {
  int32_t shard_id{0};
  int64_t start{0};
  int64_t end{0};
  int64_t title_off{0};
  int32_t title_len{0};
};

// Minimal XML extraction of <title>...</title> and <text ...>...</text> per <page>.
// Streaming: assumes Wikipedia dump structure. Not a full XML parser.
static bool stream_extract_pages(FILE* in,
                                 const std::function<bool(const std::string& title, const std::string& text)>& on_page,
                                 int64_t max_pages,
                                 WikiIngestStats* stats,
                                 std::string* err) {
  std::array<char, 1 << 16> buf{};
  std::string acc;
  acc.reserve(1 << 20);

  bool in_page = false;
  std::string page_buf;
  page_buf.reserve(1 << 20);

  while (true) {
    const size_t n = fread(buf.data(), 1, buf.size(), in);
    if (n == 0) break;
    acc.append(buf.data(), n);

    size_t pos = 0;
    while (true) {
      if (!in_page) {
        const size_t p0 = acc.find("<page>", pos);
        if (p0 == std::string::npos) break;
        in_page = true;
        page_buf.clear();
        pos = p0;
      }
      if (in_page) {
        const size_t p1 = acc.find("</page>", pos);
        if (p1 == std::string::npos) break;
        const size_t end = p1 + 7;
        page_buf.append(acc.substr(pos, end - pos));
        in_page = false;

        // Extract title and text.
        auto extract_tag = [&](const std::string& open, const std::string& close) -> std::string {
          const size_t a = page_buf.find(open);
          if (a == std::string::npos) return {};
          const size_t b = page_buf.find(close, a + open.size());
          if (b == std::string::npos) return {};
          return page_buf.substr(a + open.size(), b - (a + open.size()));
        };

        std::string title = extract_tag("<title>", "</title>");
        // text tag has attributes, search "<text" then ">" then "</text>"
        std::string text;
        {
          const size_t a = page_buf.find("<text");
          if (a != std::string::npos) {
            const size_t gt = page_buf.find('>', a);
            const size_t b = page_buf.find("</text>", gt == std::string::npos ? a : gt);
            if (gt != std::string::npos && b != std::string::npos && b > gt) {
              text = page_buf.substr(gt + 1, b - (gt + 1));
            }
          }
        }

        if (stats) stats->pages_seen += 1;
        if (!title.empty() && !text.empty()) {
          if (!on_page(title, text)) return false;
          if (stats) stats->pages_indexed += 1;
        }
        if (max_pages > 0 && stats && stats->pages_indexed >= max_pages) return true;

        pos = end;
      }
    }

    // Keep tail to avoid unbounded acc growth.
    if (!in_page) {
      if (acc.size() > (1 << 20)) {
        acc.erase(0, acc.size() - (1 << 20));
      }
    } else {
      // If inside a page, move content since last "<page>" into page_buf and keep remaining.
      const size_t p0 = acc.rfind("<page>");
      if (p0 != std::string::npos) {
        page_buf.append(acc.substr(p0));
        acc.clear();
      } else if (acc.size() > (1 << 20)) {
        // Worst-case: prevent memory blow-up.
        acc.erase(0, acc.size() - (1 << 20));
      }
    }
  }

  if (ferror(in)) {
    if (err) *err = "read error while streaming dump";
    return false;
  }
  if (err) err->clear();
  return true;
}

static FILE* popen_bzip2(const std::string& bz2_path, std::string* err) {
  // Use system bzip2 to stream-decompress. This keeps deps minimal.
  std::ostringstream cmd;
  // Redirect stderr to avoid "Broken pipe" noise if the consumer stops early.
  cmd << "bzip2 -dc " << "'" << bz2_path << "'" << " 2>/dev/null";
  FILE* p = popen(cmd.str().c_str(), "r");
  if (!p) {
    if (err) *err = std::string("failed to spawn bzip2: ") + ::strerror(errno);
  }
  return p;
}

bool ingest_wikipedia(const WikiIngestConfig& cfg, WikiIngestStats* stats, std::string* err) {
  if (cfg.dump_bz2_path.empty()) {
    if (err) *err = "dump path is empty";
    return false;
  }
  if (cfg.out_dir.empty()) {
    if (err) *err = "out_dir is empty";
    return false;
  }
  if (cfg.hash_dims <= 0) {
    if (err) *err = "hash_dims must be > 0";
    return false;
  }

  try {
    fs::create_directories(cfg.out_dir);
    fs::create_directories(fs::path(cfg.out_dir) / "shards");
    fs::create_directories(fs::path(cfg.out_dir) / "postings");
  } catch (const std::exception& ex) {
    if (err) *err = std::string("failed to create output dirs: ") + ex.what();
    return false;
  }

  // Compact on-disk index for fast retrieval:
  // - shards.txt: shard_id\tfilename
  // - titles.bin: concatenated UTF-8 titles
  // - chunks.bin: fixed-size record per chunk_id (record index == chunk_id)
  std::ofstream shards_out(fs::path(cfg.out_dir) / "shards.txt", std::ios::binary | std::ios::trunc);
  std::ofstream titles_out(fs::path(cfg.out_dir) / "titles.bin", std::ios::binary | std::ios::trunc);
  std::ofstream chunks_out(fs::path(cfg.out_dir) / "chunks.bin", std::ios::binary | std::ios::trunc);
  if (!shards_out || !titles_out || !chunks_out) {
    if (err) *err = "failed to open wiki index output files";
    return false;
  }

  // Posting files per dim: dim files each line "chunk_id\tfreq"
  std::vector<std::ofstream> posting_files(static_cast<size_t>(cfg.hash_dims));
  for (int d = 0; d < cfg.hash_dims; ++d) {
    std::ostringstream name;
    name << std::setw(4) << std::setfill('0') << d << ".tsv";
    posting_files[static_cast<size_t>(d)].open(fs::path(cfg.out_dir) / "postings" / name.str(),
                                               std::ios::binary | std::ios::trunc);
    if (!posting_files[static_cast<size_t>(d)]) {
      if (err) *err = "failed to open postings file for dim " + std::to_string(d);
      return false;
    }
  }

  ShardWriter shard;
  shard.dir = fs::path(cfg.out_dir) / "shards";
  shard.shard_target_mb = cfg.shard_target_mb;
  std::string e;
  fs::path shard_path = shard.open_next(&e);
  if (!e.empty()) {
    if (err) *err = e;
    return false;
  }
  shards_out << shard.current_shard_id << "\t" << shard_path.filename().string() << "\n";

  int64_t chunk_id = 0;

  FILE* in = popen_bzip2(cfg.dump_bz2_path, &e);
  if (!in) {
    if (err) *err = e;
    return false;
  }

  auto on_page = [&](const std::string& title_raw, const std::string& text_raw) -> bool {
    std::string title = title_raw;
    std::string text = strip_simple_markup(text_raw);
    if (text.size() < 200) return true; // skip stubs

    // Rotate shard if needed.
    if (!shard.out.is_open() || shard.bytes_in_shard > static_cast<int64_t>(cfg.shard_target_mb) * 1024 * 1024) {
      shard_path = shard.open_next(&e);
      if (!e.empty()) return false;
      shards_out << shard.current_shard_id << "\t" << shard_path.filename().string() << "\n";
    }

    const int64_t title_off = static_cast<int64_t>(titles_out.tellp());
    titles_out.write(title.data(), static_cast<std::streamsize>(title.size()));
    const int32_t title_len = static_cast<int32_t>(title.size());

    const auto ranges = chunk_ranges(static_cast<int64_t>(text.size()), cfg.chunk_bytes, cfg.overlap_bytes);
    for (size_t ci = 0; ci < ranges.size(); ++ci) {
      const auto [a, b] = ranges[ci];
      const std::string chunk_text = text.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));

      const std::streampos start = shard.out.tellp();
      shard.out << "TITLE: " << title << "\n";
      shard.out << "CHUNK: " << ci << "\n";
      shard.out << chunk_text << "\n";
      shard.out << "----\n";
      const std::streampos end = shard.out.tellp();

      const int64_t s = static_cast<int64_t>(start);
      const int64_t en = static_cast<int64_t>(end);
      shard.bytes_in_shard += (en - s);
      if (stats) {
        stats->chunks_written += 1;
        stats->bytes_written += (en - s);
      }

      ChunkRec rec;
      rec.shard_id = static_cast<int32_t>(shard.current_shard_id);
      rec.start = s;
      rec.end = en;
      rec.title_off = title_off;
      rec.title_len = title_len;
      chunks_out.write(reinterpret_cast<const char*>(&rec), sizeof(rec));

      // Build hashed postings for this chunk.
      std::unordered_map<int, int> tf;
      const auto toks = tokenize(chunk_text);
      tf.reserve(toks.size());
      for (const auto& t : toks) {
        const int dim = static_cast<int>(fnv1a64(t) % static_cast<uint64_t>(cfg.hash_dims));
        tf[dim] += 1;
      }
      for (const auto& [dim, freq] : tf) {
        posting_files[static_cast<size_t>(dim)] << chunk_id << "\t" << freq << "\n";
      }

      chunk_id++;
    }
    return true;
  };

  const bool ok = stream_extract_pages(in, on_page, cfg.max_pages, stats, &e);
  // If we stop early (max_pages), bzip2 will see a broken pipe. Suppress that noise.
  if (cfg.max_pages > 0 && stats && stats->pages_indexed >= cfg.max_pages) {
    pclose(in);
  } else {
    pclose(in);
  }
  if (!ok) {
    if (err) *err = e.empty() ? "wiki ingest failed" : e;
    return false;
  }
  if (!e.empty()) {
    if (err) *err = e;
    return false;
  }

  if (err) err->clear();
  return true;
}

} // namespace tempest::wiki

