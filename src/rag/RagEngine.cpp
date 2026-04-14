#include "tempest/rag/RagEngine.h"

#include "tempest/core/Strings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>

namespace fs = std::filesystem;

namespace tempest::rag {

bool RagEngine::available() const {
  // This RAG implementation is self-contained and works without external deps.
  return true;
}

void RagEngine::add_fallback_chunk_dir(const std::string& dir) {
  if (dir.empty()) return;
  fallback_dirs_.push_back(dir);
}

void RagEngine::set_wiki_index_dir(const std::string& dir) {
  wiki_dir_ = dir;
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

struct ChunkDoc {
  std::string file_path;
  int chunk_index{0};
  std::string text;
};

static uint64_t fnv1a64(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

static bool parse_fallback_chunk_file(const fs::path& p, std::vector<ChunkDoc>* out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::string line;
  std::string cur_text;
  int cur_idx = -1;
  std::string src_path;

  auto flush = [&]() {
    if (cur_idx >= 0 && !cur_text.empty()) {
      ChunkDoc d;
      d.file_path = src_path.empty() ? p.string() : src_path;
      d.chunk_index = cur_idx;
      d.text = cur_text;
      out->push_back(std::move(d));
    }
    cur_text.clear();
  };

  while (std::getline(in, line)) {
    if (tempest::core::starts_with(line, "path: ")) {
      src_path = line.substr(6);
      continue;
    }
    if (tempest::core::starts_with(line, "[chunk ")) {
      flush();
      // [chunk N bytes A-B]
      std::istringstream iss(line);
      std::string bracket, chunk_word;
      iss >> bracket >> cur_idx; // "[chunk" then index
      if (iss.fail()) cur_idx = -1;
      continue;
    }
    if (line == "----") {
      flush();
      cur_idx = -1;
      continue;
    }
    if (cur_idx >= 0) {
      cur_text += line;
      cur_text.push_back('\n');
    }
  }
  flush();
  return true;
}

static double bm25_score(const std::unordered_map<std::string, int>& qtf,
                         const std::unordered_map<std::string, int>& dtf,
                         const std::unordered_map<std::string, int>& df,
                         int64_t N,
                         int doc_len,
                         double avgdl) {
  const double k1 = 1.2;
  const double b = 0.75;
  double score = 0.0;
  for (const auto& [term, qf] : qtf) {
    (void)qf;
    auto itf = dtf.find(term);
    if (itf == dtf.end()) continue;
    const int f = itf->second;
    const int dfi = std::max(1, df.at(term));
    const double idf = std::log(1.0 + (N - dfi + 0.5) / (dfi + 0.5));
    const double denom = f + k1 * (1.0 - b + b * (static_cast<double>(doc_len) / std::max(1.0, avgdl)));
    score += idf * (f * (k1 + 1.0)) / denom;
  }
  return score;
}

RagResult RagEngine::retrieve(const std::string& query, int top_k) const {
  RagResult r;
  if (top_k <= 0) top_k = 5;

  // Default fallback directory (created by LocalLibrary when SQLite isn't available).
  std::vector<std::string> dirs = fallback_dirs_;
  if (dirs.empty()) dirs.push_back(".tempest_local_index/chunks");

  std::vector<ChunkDoc> docs;
  for (const auto& d : dirs) {
    fs::path base(d);
    if (!fs::exists(base) || !fs::is_directory(base)) continue;
    for (const auto& entry : fs::directory_iterator(base)) {
      if (!entry.is_regular_file()) continue;
      (void)parse_fallback_chunk_file(entry.path(), &docs);
      if (docs.size() > 5000) break; // safety limit for now
    }
  }

  const auto qterms = tokenize(query);
  if (qterms.empty()) return r;

  std::unordered_map<std::string, int> qtf;
  qtf.reserve(qterms.size());
  for (const auto& t : qterms) qtf[t] += 1;

  // Compute df over this corpus for query terms only.
  std::unordered_map<std::string, int> df;
  df.reserve(qtf.size());
  std::vector<int> doc_lens;
  doc_lens.reserve(docs.size());

  std::vector<std::unordered_map<std::string, int>> dtfs;
  dtfs.reserve(docs.size());

  int64_t total_len = 0;
  for (const auto& doc : docs) {
    const auto terms = tokenize(doc.text);
    total_len += static_cast<int64_t>(terms.size());
    doc_lens.push_back(static_cast<int>(terms.size()));

    std::unordered_map<std::string, int> dtf;
    dtf.reserve(terms.size());
    for (const auto& t : terms) {
      if (qtf.find(t) != qtf.end()) dtf[t] += 1;
    }
    std::unordered_set<std::string> seen;
    for (const auto& [t, f] : dtf) {
      (void)f;
      if (seen.insert(t).second) df[t] += 1;
    }
    dtfs.push_back(std::move(dtf));
  }
  for (const auto& [t, _] : qtf) {
    if (df.find(t) == df.end()) df[t] = 0;
  }

  std::ostringstream ctx;
  int n = 0;

  // 1) Local fallback corpus (small): compute BM25 by scanning docs.
  if (!docs.empty()) {
    const double avgdl = docs.empty() ? 1.0 : (static_cast<double>(total_len) / docs.size());
    struct ScoredLocal { double s; size_t i; };
    std::vector<ScoredLocal> scored;
    scored.reserve(docs.size());
    for (size_t i = 0; i < docs.size(); ++i) {
      if (dtfs[i].empty()) continue;
      double s = bm25_score(qtf, dtfs[i], df, static_cast<int64_t>(docs.size()), doc_lens[i], avgdl);
      if (s > 0.0) scored.push_back({s, i});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.s > b.s; });
    if (static_cast<int>(scored.size()) > top_k) scored.resize(static_cast<size_t>(top_k));

    for (const auto& s : scored) {
      const auto& d = docs[s.i];
      RetrievedChunk c;
      c.source = "local";
      c.title = d.file_path + " (chunk " + std::to_string(d.chunk_index) + ")";
      c.text = d.text;
      c.score = s.s;
      r.chunks.push_back(c);

      ctx << "SOURCE " << (++n) << ": " << c.title << "\n";
      ctx << c.text << "\n";
      ctx << "----\n";
    }
  }

  // 2) Wikipedia corpus: hashed inverted index lookup (fast enough for large corpora).
  if (!wiki_dir_.empty()) {
    fs::path base(wiki_dir_);
    fs::path postings_dir = base / "postings";
    fs::path chunks_bin = base / "chunks.bin";
    fs::path titles_bin = base / "titles.bin";
    fs::path shards_txt = base / "shards.txt";
    fs::path shards_dir = base / "shards";
    if (fs::exists(postings_dir) && fs::exists(chunks_bin) && fs::exists(titles_bin) && fs::exists(shards_txt)) {
      // Load shard id -> filename map.
      std::unordered_map<int, std::string> shard_map;
      {
        std::ifstream s_in(shards_txt);
        std::string line;
        while (std::getline(s_in, line)) {
          auto parts = tempest::core::split_ws(line);
          if (parts.size() < 2) continue;
          // shards.txt uses tab, split_ws is ok for this.
          int sid = std::stoi(parts[0]);
          shard_map[sid] = parts[1];
        }
      }

      // Score chunks by summing tf for hashed dims from query terms.
      const int hash_dims = 4096; // matches ingestor default
      std::unordered_set<int> dims;
      for (const auto& t : qterms) {
        dims.insert(static_cast<int>(fnv1a64(t) % static_cast<uint64_t>(hash_dims)));
      }

      std::unordered_map<int64_t, double> score;
      for (int dim : dims) {
        std::ostringstream name;
        name << std::setw(4) << std::setfill('0') << dim << ".tsv";
        fs::path p = postings_dir / name.str();
        std::ifstream in(p);
        if (!in) continue;
        int64_t cid = 0;
        int tf = 0;
        while (in >> cid >> tf) {
          score[cid] += static_cast<double>(tf);
        }
      }

      // Select top candidates.
      struct ScoredW { double s; int64_t id; };
      std::vector<ScoredW> top;
      top.reserve(score.size());
      for (const auto& [id, s] : score) {
        if (s > 0.0) top.push_back({s, id});
      }
      std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.s > b.s; });
      if (static_cast<int>(top.size()) > top_k) top.resize(static_cast<size_t>(top_k));

      // Read chunk records and materialize text.
      struct ChunkRec { int32_t shard_id; int64_t start; int64_t end; int64_t title_off; int32_t title_len; };
      std::ifstream cbin(chunks_bin, std::ios::binary);
      std::ifstream tbin(titles_bin, std::ios::binary);
      for (const auto& s : top) {
        const int64_t cid = s.id;
        const std::streamoff off = static_cast<std::streamoff>(cid) * static_cast<std::streamoff>(sizeof(ChunkRec));
        cbin.seekg(off);
        ChunkRec rec{};
        cbin.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!cbin) continue;

        std::string title;
        title.resize(static_cast<size_t>(std::max(0, rec.title_len)));
        tbin.seekg(static_cast<std::streamoff>(rec.title_off));
        tbin.read(&title[0], rec.title_len);
        if (!tbin) title = "(unknown)";

        const auto it = shard_map.find(rec.shard_id);
        if (it == shard_map.end()) continue;
        fs::path shard_path = shards_dir / it->second;
        std::ifstream shard_in(shard_path, std::ios::binary);
        if (!shard_in) continue;
        const int64_t len = rec.end - rec.start;
        if (len <= 0 || len > 2000000) continue;
        std::string text;
        text.resize(static_cast<size_t>(len));
        shard_in.seekg(static_cast<std::streamoff>(rec.start));
        shard_in.read(&text[0], static_cast<std::streamsize>(len));
        if (!shard_in) continue;

        RetrievedChunk c;
        c.source = "wikipedia";
        c.title = title;
        c.text = text;
        c.score = s.s;
        r.chunks.push_back(c);

        ctx << "SOURCE " << (++n) << ": [wikipedia] " << c.title << " (chunk_id " << cid << ")\n";
        ctx << c.text << "\n";
        ctx << "----\n";
      }
    }
  }

  r.synthesized_context = ctx.str();
  return r;
}

} // namespace tempest::rag

