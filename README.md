# Tempest (v1.13 skeleton)

This repository is a **from-scratch C++17 skeleton** for Tempest v1.13 as described in `description.txt`.

## Build

```bash
./build.sh
./build/tempest
```

If you prefer manual CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/tempest
```

## Commands (skeleton)

- `/help`
- `/exit`
- `/clear`
- `/reset`
- `/info`
- `/switch` (toggles 0.8b ↔ 3b)
- `/rag <question>` (stub unless built)
- `/telegram` (stub unless built)
- `/add <path>` (index local `.txt`)
- `/watch <dir>` (auto-index local `.txt`)

## Feature flags

- `-DTEMPEST_ENABLE_LLAMA=ON` (llama.cpp integration placeholder)
- `-DTEMPEST_ENABLE_RAG=ON` (SQLite + HNSW RAG placeholder)
- `-DTEMPEST_ENABLE_TELEGRAM=ON` (Telegram long-poll placeholder)

## Wikipedia ingest (offline)

This project can build a local Wikipedia RAG index from the dump:

```bash
./build/tempest --wiki-ingest enwiki-latest-pages-articles.xml.bz2 --out .tempest_wiki_index
```

For a quick test:

```bash
./build/tempest --wiki-ingest enwiki-latest-pages-articles.xml.bz2 --out .tempest_wiki_index --max-pages 50
./build/tempest
```

Then in Tempest:

```text
/rag anarchism
```

