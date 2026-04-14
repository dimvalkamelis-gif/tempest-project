cmake_minimum_required(VERSION 3.20)

project(tempest VERSION 1.13.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TEMPEST_ENABLE_TELEGRAM "Enable Telegram integration (curl long-poll)" ON)
option(TEMPEST_ENABLE_RAG "Enable RAG pipeline (stub by default)" OFF)
option(TEMPEST_ENABLE_LLAMA "Enable llama.cpp integration (llama-cli runner)" OFF)
set(TEMPEST_LLAMA_VENDOR_DIR "${CMAKE_SOURCE_DIR}/third_party/llama.cpp" CACHE PATH "Path to a vendored llama.cpp checkout")
option(TEMPEST_ENABLE_SQLITE "Enable SQLite backing for local library" ON)

if (TEMPEST_ENABLE_LLAMA)
  # Heuristic: require a full llama.cpp checkout layout.
  if (EXISTS "${TEMPEST_LLAMA_VENDOR_DIR}/CMakeLists.txt"
      AND EXISTS "${TEMPEST_LLAMA_VENDOR_DIR}/src/CMakeLists.txt"
      AND EXISTS "${TEMPEST_LLAMA_VENDOR_DIR}/ggml/CMakeLists.txt")
    # Build llama.cpp's CLI and call it from Tempest. This avoids chasing the fast-moving C API
    # while still producing real model output.
    set(LLAMA_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
    add_subdirectory("${TEMPEST_LLAMA_VENDOR_DIR}" third_party_llama_cpp EXCLUDE_FROM_ALL)
  else()
    message(WARNING "TEMPEST_ENABLE_LLAMA=ON but vendored llama.cpp not found at: ${TEMPEST_LLAMA_VENDOR_DIR}. Set TEMPEST_LLAMA_VENDOR_DIR or vendor llama.cpp into third_party/llama.cpp.")
    set(TEMPEST_ENABLE_LLAMA OFF)
  endif()
endif()

add_executable(tempest
  src/main.cpp
  src/app/CommandRouter.cpp
  src/core/Ansi.cpp
  src/core/Banner.cpp
  src/core/BuildInfo.cpp
  src/core/Clock.cpp
  src/core/Config.cpp
  src/core/Strings.cpp
  src/library/LocalLibrary.cpp
  src/store/SqliteStore.cpp
  src/models/LlamaCli.cpp
  src/models/LlamaEngine.cpp
  src/models/ModelManager.cpp
  src/rag/RagEngine.cpp
  src/wiki/WikiIngestor.cpp
  src/telegram/TelegramGateway.cpp
  src/telegram/TelegramHttp.cpp
  src/ui/TerminalUI.cpp
)

target_include_directories(tempest PRIVATE include)

target_compile_definitions(tempest PRIVATE
  TEMPEST_VERSION_STRING="${PROJECT_VERSION}"
)

# Make the binary runnable without manual LD_LIBRARY_PATH when llama.cpp
# builds shared libraries into build/bin/.
set_target_properties(tempest PROPERTIES
  BUILD_RPATH "$ORIGIN/bin"
  INSTALL_RPATH "$ORIGIN/bin"
)

if (TEMPEST_ENABLE_TELEGRAM)
  target_compile_definitions(tempest PRIVATE TEMPEST_ENABLE_TELEGRAM=1)
endif()
if (TEMPEST_ENABLE_RAG)
  target_compile_definitions(tempest PRIVATE TEMPEST_ENABLE_RAG=1)
endif()
if (TEMPEST_ENABLE_LLAMA)
  target_compile_definitions(tempest PRIVATE TEMPEST_ENABLE_LLAMA=1)
  # Link llama.cpp directly for speed/stability (no per-prompt process spawn).
  if (TARGET llama)
    target_link_libraries(tempest PRIVATE llama)
    target_include_directories(tempest PRIVATE "${TEMPEST_LLAMA_VENDOR_DIR}/include")
  endif()
  # Newer llama.cpp builds example binary `llama-simple` (accepts -m -n [prompt]).
  if (TARGET llama-simple)
    add_dependencies(tempest llama-simple)
    target_compile_definitions(tempest PRIVATE TEMPEST_LLAMA_RUNNER_PATH="$<TARGET_FILE:llama-simple>")
  endif()
endif()

if (TEMPEST_ENABLE_SQLITE)
  find_package(SQLite3)
  if (SQLite3_FOUND)
    target_compile_definitions(tempest PRIVATE TEMPEST_ENABLE_SQLITE=1)
    target_include_directories(tempest PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(tempest PRIVATE ${SQLite3_LIBRARIES})
  else()
    message(WARNING "SQLite3 not found; local library will run in memory only.")
  endif()
endif()

if (MSVC)
  target_compile_options(tempest PRIVATE /W4)
else()
  target_compile_options(tempest PRIVATE -Wall -Wextra -Wpedantic)
endif()

