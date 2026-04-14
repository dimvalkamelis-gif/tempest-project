#pragma once

#include <cstdint>
#include <string>

namespace tempest::wiki {

struct WikiIngestConfig {
  std::string dump_bz2_path;
  std::string out_dir;          // e.g. ".tempest_wiki_index"
  int64_t max_pages{-1};        // -1 = no limit
  int shard_target_mb{64};      // shard file size target
  int chunk_bytes{8000};
  int overlap_bytes{800};
  int hash_dims{4096};          // hashed inverted index dims
};

struct WikiIngestStats {
  int64_t pages_seen{0};
  int64_t pages_indexed{0};
  int64_t chunks_written{0};
  int64_t bytes_written{0};
};

bool ingest_wikipedia(const WikiIngestConfig& cfg, WikiIngestStats* stats, std::string* err);

} // namespace tempest::wiki

