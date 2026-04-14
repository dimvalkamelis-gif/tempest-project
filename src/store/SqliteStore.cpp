#include "tempest/store/SqliteStore.h"

#include <sstream>

#if defined(TEMPEST_ENABLE_SQLITE) && TEMPEST_ENABLE_SQLITE
#  include <sqlite3.h>
#endif

namespace tempest::store {

SqliteStore::SqliteStore(std::string db_path) : db_path_(std::move(db_path)) {}

bool SqliteStore::open_or_create(std::string* err) {
#if !(defined(TEMPEST_ENABLE_SQLITE) && TEMPEST_ENABLE_SQLITE)
  if (err) *err = "sqlite support not compiled";
  return false;
#else
  if (db_) return true;
  sqlite3* db = nullptr;
  if (sqlite3_open(db_path_.c_str(), &db) != SQLITE_OK) {
    if (err) *err = sqlite3_errmsg(db);
    sqlite3_close(db);
    return false;
  }
  db_ = db;

  const char* ddl =
      "PRAGMA journal_mode=WAL;"
      "CREATE TABLE IF NOT EXISTS files ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  path TEXT NOT NULL UNIQUE,"
      "  mtime_unix INTEGER NOT NULL,"
      "  size_bytes INTEGER NOT NULL,"
      "  sha256_hex TEXT,"
      "  added_at_unix INTEGER NOT NULL"
      ");"
      "CREATE TABLE IF NOT EXISTS chunks ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  file_id INTEGER NOT NULL,"
      "  chunk_index INTEGER NOT NULL,"
      "  offset_start INTEGER NOT NULL,"
      "  offset_end INTEGER NOT NULL,"
      "  text TEXT NOT NULL,"
      "  FOREIGN KEY(file_id) REFERENCES files(id)"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_chunks_file ON chunks(file_id);";

  char* emsg = nullptr;
  if (sqlite3_exec(static_cast<sqlite3*>(db_), ddl, nullptr, nullptr, &emsg) != SQLITE_OK) {
    if (err) *err = emsg ? std::string(emsg) : "sqlite exec failed";
    sqlite3_free(emsg);
    return false;
  }
  return true;
#endif
}

bool SqliteStore::file_needs_reindex(const std::string& path, long long mtime_unix, long long size_bytes, bool* out_needs, std::string* err) {
#if !(defined(TEMPEST_ENABLE_SQLITE) && TEMPEST_ENABLE_SQLITE)
  (void)path; (void)mtime_unix; (void)size_bytes;
  if (out_needs) *out_needs = true;
  if (err) err->clear();
  return true;
#else
  if (!open_or_create(err)) return false;
  const char* sql = "SELECT mtime_unix, size_bytes FROM files WHERE path = ?1;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &st, nullptr) != SQLITE_OK) {
    if (err) *err = sqlite3_errmsg(static_cast<sqlite3*>(db_));
    return false;
  }
  sqlite3_bind_text(st, 1, path.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW) {
    const long long prev_mtime = sqlite3_column_int64(st, 0);
    const long long prev_size = sqlite3_column_int64(st, 1);
    if (out_needs) *out_needs = (prev_mtime != mtime_unix) || (prev_size != size_bytes);
  } else if (rc == SQLITE_DONE) {
    if (out_needs) *out_needs = true;
  } else {
    if (err) *err = sqlite3_errmsg(static_cast<sqlite3*>(db_));
    sqlite3_finalize(st);
    return false;
  }
  sqlite3_finalize(st);
  if (err) err->clear();
  return true;
#endif
}

bool SqliteStore::upsert_file(const StoredFile& f, long long* out_file_id, std::string* err) {
#if !(defined(TEMPEST_ENABLE_SQLITE) && TEMPEST_ENABLE_SQLITE)
  (void)f;
  (void)out_file_id;
  if (err) *err = "sqlite support not compiled";
  return false;
#else
  if (!open_or_create(err)) return false;
  const char* sql =
      "INSERT INTO files(path, mtime_unix, size_bytes, sha256_hex, added_at_unix) "
      "VALUES(?1, ?2, ?3, ?4, strftime('%s','now')) "
      "ON CONFLICT(path) DO UPDATE SET "
      "  mtime_unix=excluded.mtime_unix,"
      "  size_bytes=excluded.size_bytes,"
      "  sha256_hex=excluded.sha256_hex;";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &st, nullptr) != SQLITE_OK) {
    if (err) *err = sqlite3_errmsg(static_cast<sqlite3*>(db_));
    return false;
  }
  sqlite3_bind_text(st, 1, f.path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 2, f.mtime_unix);
  sqlite3_bind_int64(st, 3, f.size_bytes);
  if (f.sha256_hex.empty()) {
    sqlite3_bind_null(st, 4);
  } else {
    sqlite3_bind_text(st, 4, f.sha256_hex.c_str(), -1, SQLITE_TRANSIENT);
  }

  if (sqlite3_step(st) != SQLITE_DONE) {
    if (err) *err = sqlite3_errmsg(static_cast<sqlite3*>(db_));
    sqlite3_finalize(st);
    return false;
  }
  sqlite3_finalize(st);

  // Fetch id
  const char* sql2 = "SELECT id FROM files WHERE path = ?1;";
  sqlite3_stmt* st2 = nullptr;
  if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql2, -1, &st2, nullptr) != SQLITE_OK) {
    if (err) *err = sqlite3_errmsg(static_cast<sqlite3*>(db_));
    return false;
  }
  sqlite3_bind_text(st2, 1, f.path.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st2) == SQLITE_ROW) {
    if (out_file_id) *out_file_id = sqlite3_column_int64(st2, 0);
  } else {
    if (err) *err = "failed to read file id";
    sqlite3_finalize(st2);
    return false;
  }
  sqlite3_finalize(st2);
  if (err) err->clear();
  return true;
#endif
}

bool SqliteStore::replace_chunks(long long file_id, const std::vector<StoredChunk>& chunks, std::string* err) {
#if !(defined(TEMPEST_ENABLE_SQLITE) && TEMPEST_ENABLE_SQLITE)
  (void)file_id; (void)chunks;
  if (err) *err = "sqlite support not compiled";
  return false;
#else
  if (!open_or_create(err)) return false;
  sqlite3* db = static_cast<sqlite3*>(db_);

  char* emsg = nullptr;
  if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, &emsg) != SQLITE_OK) {
    if (err) *err = emsg ? std::string(emsg) : "BEGIN failed";
    sqlite3_free(emsg);
    return false;
  }

  {
    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM chunks WHERE file_id = ?1;", -1, &del, nullptr) != SQLITE_OK) {
      if (err) *err = sqlite3_errmsg(db);
      sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return false;
    }
    sqlite3_bind_int64(del, 1, file_id);
    if (sqlite3_step(del) != SQLITE_DONE) {
      if (err) *err = sqlite3_errmsg(db);
      sqlite3_finalize(del);
      sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return false;
    }
    sqlite3_finalize(del);
  }

  sqlite3_stmt* ins = nullptr;
  if (sqlite3_prepare_v2(db,
        "INSERT INTO chunks(file_id, chunk_index, offset_start, offset_end, text) VALUES(?1, ?2, ?3, ?4, ?5);",
        -1, &ins, nullptr) != SQLITE_OK) {
    if (err) *err = sqlite3_errmsg(db);
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }

  for (const auto& c : chunks) {
    sqlite3_reset(ins);
    sqlite3_clear_bindings(ins);
    sqlite3_bind_int64(ins, 1, file_id);
    sqlite3_bind_int(ins, 2, c.chunk_index);
    sqlite3_bind_int64(ins, 3, c.offset_start);
    sqlite3_bind_int64(ins, 4, c.offset_end);
    sqlite3_bind_text(ins, 5, c.text.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(ins) != SQLITE_DONE) {
      if (err) *err = sqlite3_errmsg(db);
      sqlite3_finalize(ins);
      sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return false;
    }
  }
  sqlite3_finalize(ins);

  if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &emsg) != SQLITE_OK) {
    if (err) *err = emsg ? std::string(emsg) : "COMMIT failed";
    sqlite3_free(emsg);
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
  }
  if (err) err->clear();
  return true;
#endif
}

} // namespace tempest::store

