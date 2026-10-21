# MiniRedis

A single-node, in-memory key-value server written in **C++17** that speaks the **Redis wire protocol (RESP2)**. Compatible with official `redis-cli` and `redis-benchmark`, featuring hand-written core data structures, non-blocking I/O multiplexing, and an interactive modern web dashboard.

---

## 1. Overview

MiniRedis is an educational, interview-grade implementation of an in-memory key-value database built from the ground up in modern C++17. Every core data structure—from the hash table with incremental rehashing to the binary min-heap TTL engine—is hand-crafted without relying on `std::unordered_map` or container shortcuts in the storage layer.

### What it is:
* A fully functional RESP2-compliant server that you can query with the real `redis-cli` binary.
* A demonstration of systems programming: non-blocking sockets, event multiplexing (`poll`), buffer accumulation, and progressive rehashing.
* Accompanied by a lightweight **Python FastAPI bridge** and a **live Web Dashboard** with an in-browser terminal and real-time keyspace visualizer.

### What it is not:
* **Not production software**: It does not support clustering, replication, Lua scripting, transactions (`MULTI`/`EXEC`), pub/sub, TLS, or ACLs.
* **Not built to beat real Redis in throughput**: The primary objective is architectural clarity, memory safety, and explainability in technical interviews.

---

## 2. Architecture

```text
┌───────────────────────────┐         ┌───────────────────────────────┐
│     Official Clients      │         │         Web Browser           │
│ (redis-cli, redis-py, …)  │         │ (Vanilla HTML / CSS / JS UI)  │
└─────────────┬─────────────┘         └───────────────┬───────────────┘
              │                                       │ HTTP / REST
              │ RESP2 over TCP                        ▼
              │                        ┌──────────────────────────────┐
              │                        │   FastAPI Bridge (api.py)    │
              │                        │      Port 8000               │
              │                        └──────────────┬───────────────┘
              ▼                                       │ RESP2 over TCP
  ┌───────────────────────────────────────────────────▼───────────────┐
  │                   MiniRedis Server (Port 6380)                    │
  │                                                                   │
  │  ┌─────────────────────────────────────────────────────────────┐  │
  │  │               POSIX poll() Non-Blocking Event Loop          │  │
  │  └──────────────┬───────────────────────────────▲──────────────┘  │
  │                 │ Inbound stream                │ Outbound replies│
  │                 ▼                               │                 │
  │  ┌──────────────────────────────┐   ┌───────────┴──────────────┐  │
  │  │        RESP2 Parser          │   │      RESP2 Encoder       │  │
  │  │  (split reads & pipelining)  │   │ (+OK, :int, $bulk, -ERR) │  │
  │  └──────────────┬───────────────┘   └───────────▲──────────────┘  │
  │                 │ Parsed Command                │ Reply String    │
  │                 ▼                               │                 │
  │  ┌──────────────────────────────────────────────┴──────────────┐  │
  │  │                     Command Dispatcher                      │  │
  │  │  (PING, SET, GET, INCR, LPUSH, LRANGE, TTL, KEYS, …)        │  │
  │  └──────────────┬──────────────────────────────────────────────┘  │
  │                 │ Mutates / Queries                               │
  │                 ▼                                                 │
  │  ┌─────────────────────────────────────────────────────────────┐  │
  │  │                     Central Store                           │  │
  │  │                                                             │  │
  │  │  • HashTable<Value>: Separate chaining, FNV-1a hash,       │  │
  │  │    incremental 8-bucket progressive rehashing               │  │
  │  │  • ExpiryHeap: Binary min-heap for O(log N) TTL deadlines   │  │
  │  │  • Lazy + Active Expiration (100ms periodic tick)           │  │
  │  └─────────────────────────────────────────────────────────────┘  │
  └───────────────────────────────────────────────────────────────────┘
```

---

## 3. Core Data Structures & Design Decisions

### 1. Hand-Written Hash Table (`src/store/hash_table.h`)
* **Collision Resolution**: Separate chaining using singly-linked lists of dynamically allocated nodes.
* **Hash Function**: 64-bit **FNV-1a** algorithm (fast, low collision rate, zero external dependencies).
* **Incremental Progressive Rehashing**:
  * When load factor exceeds `1.0` (`count >= capacity`), a secondary table (`tables_[1]`) is allocated with double the capacity.
  * Rather than copying all elements in one blocking pause, every mutating operation (`insert`, `find`, `erase`) migrates up to **8 buckets** from the old table to the new table.
  * Lookups seamlessly inspect both tables while rehashing is in progress.

### 2. Binary Min-Heap for TTL (`src/store/expiry_heap.h`)
* Stores `(expire_at_ms, key)` pairs ordered by earliest expiration.
* **Dual Expiration Strategy**:
  * **Lazy Expiry**: Whenever a key is accessed via `get()`, `exists()`, or mutated, its timestamp is checked. Expired keys are purged immediately on access.
  * **Active Expiry**: The server's 100ms `poll()` tick pops up to 64 overdue entries from the heap and removes them, ensuring memory from unread keys is reclaimed automatically.
  * **Lazy Deletion**: Overwritten or renewed keys simply push new entries to the heap; stale entries are safely skipped when popped.

### 3. Multiplexed Event Loop (`src/net/server.cpp`)
* Implemented using standard POSIX **`poll()`**, making the engine 100% portable across **macOS, Linux, and BSD** without platform-specific wrappers (`epoll` / `kqueue`).
* Non-blocking socket descriptors (`O_NONBLOCK`).
* Incoming stream accumulation in connection buffers with support for **split packets** and **pipelined batches** (multiple commands executed in a single loop cycle).

---

## 4. Supported Commands

| Category | Commands | Semantics & Acceptance |
| :--- | :--- | :--- |
| **Connection & Server** | `PING [msg]` | Returns `PONG` or echoes message as bulk string |
| | `INFO` | Returns engine metadata, version, and key counts |
| | `COMMAND` | Returns `*0` handshake reply for `redis-cli` compatibility |
| **Strings** | `SET key val [EX s] [PX ms] [NX] [XX]` | Supports expiration flags and `NX` / `XX` conditional set |
| | `GET key` | Returns bulk string or `(nil)` on missing/expired key |
| | `DEL key [key...]` | Returns integer count of keys deleted |
| | `EXISTS key [key...]` | Returns integer count of keys existing |
| | `MSET key val [key val...]` | Atomically sets multiple key-value pairs |
| | `MGET key [key...]` | Returns array of bulk strings |
| | `INCR key` / `DECR key` | Increments or decrements 64-bit integer values |
| | `INCRBY key step` | Increments value by integer offset |
| | `KEYS pattern` | Glob pattern matching (`*`, `?`) across active keys |
| **Expiry (TTL)** | `EXPIRE key seconds` | Sets relative timeout in seconds |
| | `PEXPIRE key ms` | Sets relative timeout in milliseconds |
| | `TTL key` | Returns remaining seconds (`-2` if missing, `-1` if persistent) |
| | `PTTL key` | Returns remaining milliseconds |
| | `PERSIST key` | Removes timeout, converting key to persistent |
| **Lists** | `LPUSH key val [val...]` | Prepends items to list; returns new length |
| | `RPUSH key val [val...]` | Appends items to list; returns new length |
| | `LPOP key` / `RPOP key` | Pops element from head or tail |
| | `LRANGE key start stop` | Returns range with support for negative indices (e.g., `0 -1`) |
| | `LLEN key` | Returns length of list or `WRONGTYPE` error |

---

## 5. Quickstart

### Prerequisites
* **C++17 Compiler**: `clang++` (macOS/Clang 14+) or `g++` (GCC 11+)
* **CMake**: Version 3.20 or later
* **Python 3**: For FastAPI dashboard bridge (`python3 -m pip install -r api/requirements.txt`)
* **Redis Tools** (optional): `redis-cli` (`brew install redis` on macOS or `apt install redis-tools` on Linux)

### Build the C++ Engine (3 Commands)

```bash
# 1. Generate build files
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile server and tests
cmake --build build -j

# 3. Start MiniRedis daemon on port 6380
./build/miniredis_server --port 6380
```

### Connect with `redis-cli`

In a separate terminal:
```bash
redis-cli -p 6380
```

```text
127.0.0.1:6380> PING
PONG
127.0.0.1:6380> SET username "bramha" EX 30
OK
127.0.0.1:6380> GET username
"bramha"
127.0.0.1:6380> TTL username
(integer) 28
127.0.0.1:6380> LPUSH pipeline "test" "build" "deploy"
(integer) 3
127.0.0.1:6380> LRANGE pipeline 0 -1
1) "deploy"
2) "build"
3) "test"
```

---

## 6. Interactive Web Dashboard

MiniRedis comes with a web dashboard featuring a dark-mode interface, an interactive terminal, live keyspace explorer, and real-time TTL countdown indicators.

```bash
# Launch the dashboard bridge
python3 -m uvicorn api.api:app --host 127.0.0.1 --port 8000
```

Open **`http://localhost:8000`** in your browser.

* **In-Browser Terminal**: Execute any supported Redis command with auto-formatted outputs and command history (Up/Down arrow navigation).
* **Live Keyspace Explorer**: Inspect all active keys in real-time, categorized with type badges (`STRING`, `LIST`), value previews, and live countdown timers.
* **Sample Data Generator**: Populate demo users, queues, counters, and session tokens with one click.
* **Key Manager**: Add or delete keys directly from the UI.

---

## 7. Testing & Verification

The test suite is built on **GoogleTest (GTest v1.14)** and is fetched automatically by CMake.

### Run Automated Unit Tests

```bash
./build/unit_tests
```

**Test Coverage Highlights:**
* **`HashTableTest`**: Basic CRUD, collisions, and heavy-load incremental rehashing over 5,000 keys with subsequent verification and deletion.
* **`RespParserTest`**: Validates complete frames, split-read byte-by-byte streaming, pipelined back-to-back commands, and inline command parsing.
* **`StoreTest`**: Validates CRUD operations, TTL lazy expiration timing, and list boundary conditions (`LPUSH`, `RPUSH`, `LRANGE`, `LPOP`).

### Sanitizer Builds

To build with memory and undefined behavior sanitizers enabled:

```bash
# AddressSanitizer (detects leaks, use-after-free, buffer overflows)
cmake -B build_asan -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_asan -j
./build_asan/unit_tests

# UndefinedBehaviorSanitizer
cmake -B build_ubsan -DENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build_ubsan -j
./build_ubsan/unit_tests
```

---

## 8. Repository Layout

```text
mini_redis/
├── CMakeLists.txt                 # CMake configuration with sanitizers & GTest
├── README.md                      # Project documentation & interview notes
├── src/                           # C++17 Core Engine
│   ├── main.cpp                   # Daemon entrypoint and signal handling
│   ├── util/
│   │   ├── config.h / .cpp        # CLI argument parser
│   │   ├── logger.h / .cpp        # Structured thread-safe logging
│   │   └── time_util.h            # Wall-clock and monotonic millisecond timers
│   ├── store/
│   │   ├── value.h                # Tagged union for Redis data types
│   │   ├── hash_table.h           # Hand-written HashTable with incremental rehash
│   │   ├── expiry_heap.h          # Binary min-heap for TTL management
│   │   └── store.h / .cpp         # Central store coordinating database state
│   ├── protocol/
│   │   ├── resp_parser.h / .cpp   # RESP2 wire protocol parser
│   │   └── resp_encoder.h / .cpp  # Protocol reply encoder
│   ├── commands/
│   │   ├── command_table.h / .cpp # Command dispatch table and handlers
│   └── net/
│       ├── connection.h           # Client socket state and buffers
│       └── server.h / .cpp        # poll() multiplexing event loop
├── api/                           # Python Backend Bridge
│   ├── api.py                     # FastAPI REST server communicating with C++
│   └── requirements.txt           # Python dependencies
├── web/                           # Frontend Dashboard
│   ├── index.html                 # Single-page application structure
│   ├── style.css                  # Dark-theme styles and animations
│   └── app.js                     # Interactive console & live sync script
└── tests/                         # Test Suite
    ├── test_hash_table.cpp        # Unit tests for custom HashTable
    ├── test_parser.cpp            # Protocol parser split-stream unit tests
    └── test_store.cpp             # Store & expiration unit tests
```

---

## 9. Key Interview Takeaways

1. **Why separate chaining instead of open addressing for the hash table?**
   * Separate chaining makes deletions straightforward without tombstone accumulation. During incremental rehashing, entire bucket chains can be unlinked and rebound without rehashing probe sequences.
2. **How does incremental rehashing prevent tail latency spikes?**
   * In naive hash tables, doubling a table with 1,000,000 items causes a multi-millisecond stop-the-world pause. Moving 8 buckets per client request amortizes rehash overhead to O(1) per operation.
3. **Why single-threaded event loop?**
   * By executing commands sequentially on a single event-driven thread, commands like `INCR`, `MSET`, and `LPUSH` are inherently atomic without requiring mutexes, eliminating race conditions and lock contention.
4. **How are split packets handled in TCP?**
   * TCP is a streaming protocol without message boundaries. The custom RESP parser checks for complete CRLF delimiters and bulk length headers; if incomplete data is detected, it preserves the buffer and waits for the next read event before advancing.
