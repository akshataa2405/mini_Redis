# PRD: MiniRedis, an In-Memory Key-Value Server in C++

Oct 21, 2026 · @Bramha

## 1. Overview

MiniRedis is a single-node, in-memory key-value server written in C++17 that speaks the Redis wire protocol (RESP2), so the real `redis-cli` and `redis-benchmark` tools can talk to it. One student builds it alone in four weeks, and it must be fully explainable by that student in an interview.

**Problem.** Entry-level software and embedded portfolios are full of tutorial clones (to-do apps, weather apps, chat apps). Interviewers cannot tell what the candidate actually understands. A candidate with a computer-engineering background and thin DSA experience needs one project that proves data structures, systems programming, networking, concurrency and testing in a single artifact.

**Vision.** A small, correct, well-tested server whose every design choice can be defended: why this hash table, why this expiry structure, why this threading model, and what the benchmarks show.

**Why this project**

- It forces real data-structure work (hash table, min-heap, doubly linked list) instead of calling a library.
- It needs sockets, non-blocking I/O and a protocol parser, which are core systems topics in both software and embedded interviews.
- Compatibility with `redis-cli` gives instant, credible validation: it works with an existing tool nobody wrote for it.
- Results are measurable (operations per second, latency percentiles, memory per key), so the resume can carry real numbers.

## 2. Goals, non-goals and success criteria

The project succeeds if a stranger can run `redis-cli` against it, see correct behaviour on the supported commands, and read benchmark numbers the builder can explain.

**Goals**

- G1: Correctly implement a defined subset of Redis commands over RESP2 (section 5).
- G2: Hand-write the core data structures; no use of `std::unordered_map` in the core store (it may be used in tests as a reference oracle).
- G3: Serve many clients concurrently with a documented, defensible threading model.
- G4: Survive restarts through an append-only file (AOF) and snapshot, with tested recovery.
- G5: Produce reproducible benchmarks and a written explanation of the results.
- G6: Reach at least 85% line coverage on the core library, plus fuzz or property tests on the parser.

**Non-goals**

- Clustering, replication, Lua scripting, pub/sub, transactions, ACLs, TLS.
- Beating real Redis on performance. The goal is understanding, not a speed record.
- Windows support. Linux is the supported target (macOS is best-effort).
- Production use. The README states this plainly.

**Success criteria**

| ID | Criterion | Target |
| --- | --- | --- |
| S1 | `redis-cli` runs every supported command with Redis-matching replies | 100% of the supported command list |
| S2 | Throughput for GET and SET with 50 clients, pipeline depth 1, on the builder's laptop | At least 50,000 ops/s (measured, then reported as-is) |
| S3 | Recovery after `kill -9` with AOF `everysec` | Loses at most the last second of writes |
| S4 | Sanitizers (ASan, UBSan, TSan) on the full test suite | Zero findings |
| S5 | Builder can explain every module without notes | Mock interview passes |

## 3. Users, personas and use cases

| Persona | Who | Needs | How MiniRedis serves them |
| --- | --- | --- | --- |
| Builder | Third-year engineering student, comfortable in C and basic C++ | A project that fills the DSA and systems gap and survives deep interview questions | Every module is small enough to hold in one head; design notes record each decision |
| Interviewer or recruiter | Engineer skimming a GitHub repo for five minutes | Quick proof of skill: clear README, passing CI, benchmark table | One-command build and test, a results table, an architecture diagram |
| Curious developer | Someone who wants to learn how Redis works | A readable, small codebase | Short modules, commented invariants, a design-decisions document |

**Primary use cases**

1. A developer starts the server, connects with `redis-cli`, runs `SET`, `GET`, `DEL`, `EXPIRE` and sees Redis-matching replies.
2. A client writes a key with a time-to-live; the key disappears on time and a later `GET` returns nil.
3. The server is killed unexpectedly and restarted; stored data comes back from the AOF and snapshot.
4. Fifty clients run `redis-benchmark` at once; the server stays correct and reports throughput.
5. A malformed or hostile client sends garbage or an oversized request; the server rejects it without crashing or leaking memory.
6. Memory reaches the configured limit; the server evicts keys by the chosen policy instead of failing.

## 4. Scope and phases

The build is cut into three tiers so a working, demoable server exists by the end of week 1, and every later week adds value without breaking it.

| Tier | Contents | Done when |
| --- | --- | --- |
| MVP (week 1) | TCP server, RESP2 parser and encoder, hash table, `PING`, `SET`, `GET`, `DEL`, `EXISTS` | `redis-cli` works for the five commands; unit tests pass |
| v1 (weeks 2 to 3) | TTL and expiry, `EXPIRE`, `TTL`, `INCR`, `DECR`, `MSET`, `MGET`, lists (`LPUSH`, `RPUSH`, `LPOP`, `LRANGE`), AOF persistence, snapshot, multi-client concurrency | Recovery and expiry tests pass; 50-client benchmark runs clean |
| v2 (week 4) | Memory limit with LRU eviction, `INFO` stats, pipelining, fuzzing, benchmark report, README and demo | All success criteria in section 2 met |
| Stretch | Sorted-set subset (`ZADD`, `ZRANGE`), hashes, `SCAN`, background snapshot through `fork()` | Only if v2 finished early |

**Cut order if time runs short:** drop stretch first, then lists, then LRU eviction. Never cut tests, persistence or the benchmark write-up; those are what make the project credible.

## 5. Functional requirements

Priority: M = must, S = should, C = could. Reply formats follow RESP2 exactly as real Redis sends them.

**Protocol and connection handling**

| ID | Requirement | Pri | Acceptance criteria |
| --- | --- | --- | --- |
| FR-001 | Listen on a configurable TCP port (default 6380) | M | Server binds; `redis-cli -p 6380 PING` returns `PONG` |
| FR-002 | Parse RESP2 arrays of bulk strings (client requests) | M | Parser handles complete, split-across-reads and pipelined requests in tests |
| FR-003 | Encode simple strings, errors, integers, bulk strings, nil and arrays | M | Byte-exact output compared with recorded real-Redis replies |
| FR-004 | Reject malformed input with an error and keep running | M | Fuzz run of 1 million random inputs causes no crash or sanitizer finding |
| FR-005 | Enforce limits: Enforce request limits: 1 MB per argument and 1,000 arguments per request (real Redis allows far more; the smaller caps keep memory use predictable) | M | Oversized request gets an error reply and the connection closes cleanly |
| FR-006 | Close idle connections after a configurable timeout (default 300 s) | S | Idle client is disconnected within 5 s of the timeout |
| FR-007 | Graceful shutdown on `SIGINT` and `SIGTERM`: flush the AOF, then exit | M | No data loss after a clean stop |

**String commands**

| ID | Command | Pri | Behaviour and acceptance |
| --- | --- | --- | --- |
| FR-010 | `PING [msg]` | M | `PONG`, or echoes `msg` as a bulk string |
| FR-011 | `SET key value [EX s] [PX ms] [NX] [XX]` | M | Stores; `OK` on success; nil if `NX` or `XX` condition fails; overwrites clear the old TTL unless specified |
| FR-012 | `GET key` | M | Bulk string, or nil if missing or expired |
| FR-013 | `DEL key [key ...]` | M | Returns the count actually deleted |
| FR-014 | `EXISTS key [key ...]` | M | Returns the count that exist |
| FR-015 | `INCR` / `DECR` / `INCRBY` | S | Integer reply; error if the value is not an integer or would overflow 64 bits |
| FR-016 | `MSET` / `MGET` | S | Atomic with respect to other clients; `MGET` returns nil for missing keys |
| FR-017 | `KEYS pattern` (glob) | C | Supports `*` and `?`; documented as O(N) |

**Expiry**

| ID | Requirement | Pri | Acceptance criteria |
| --- | --- | --- | --- |
| FR-020 | `EXPIRE`, `PEXPIRE`, `TTL`, `PTTL`, `PERSIST` | M | `TTL` returns -2 for a missing key, -1 for no expiry; values decrease with time |
| FR-021 | Lazy expiry: expired keys are never returned | M | A test sets a 100 ms TTL and checks `GET` after 150 ms |
| FR-022 | Active expiry: a background pass removes expired keys without being read | M | Memory of expired keys is reclaimed within 1 s without any access |

**Lists**

| ID | Command | Pri | Behaviour |
| --- | --- | --- | --- |
| FR-030 | `LPUSH` / `RPUSH` | S | Returns the new length; creates the list if missing |
| FR-031 | `LPOP` / `RPOP` | S | Pops one element, or nil if empty; deletes the key when the list becomes empty |
| FR-032 | `LRANGE key start stop` | S | Supports negative indices as in Redis |
| FR-033 | `LLEN` | S | Integer length |
| FR-034 | Wrong-type access returns `WRONGTYPE` error | M | Running `GET` on a list key returns the Redis-style error |

**Persistence, memory and operations**

| ID | Requirement | Pri | Acceptance criteria |
| --- | --- | --- | --- |
| FR-040 | AOF: append every write command to a log file | M | Replaying the AOF into an empty server reproduces identical data |
| FR-041 | AOF fsync policy: `always`, `everysec`, `no` | M | Configurable; default `everysec`; crash test verifies the loss window |
| FR-042 | AOF rewrite (compaction) | S | After rewrite, the file is smaller and replays to the same state |
| FR-043 | Snapshot: dump the full dataset to a file and load it at startup | S | Snapshot loads in time proportional to data size; corrupted file is detected by a checksum |
| FR-044 | `maxmemory` setting with LRU eviction | S | At the limit, least-recently-used keys are evicted; memory stays under the limit plus a documented overhead |
| FR-045 | `INFO` command with uptime, connected clients, key count, memory used, ops/s | S | Output matches the documented format |
| FR-046 | `CONFIG GET` for supported settings; config via file and command line | C | Documented keys only |
| FR-047 | Structured logging with levels | S | Log lines include timestamp, level and component |

## 6. Non-functional requirements

Targets are measured on the builder's own machine and reported with the hardware described. If a target is missed, the report says so and explains why; it is not quietly relaxed.

| ID | Area | Requirement | How measured |
| --- | --- | --- | --- |
| NFR-01 | Performance | At least 50,000 ops/s for GET and SET, 50 clients, no pipelining | `redis-benchmark -c 50 -t set,get` |
| NFR-02 | Latency | p99 under 5 ms at 50 clients, p50 under 1 ms | `redis-benchmark` percentile output |
| NFR-03 | Pipelining | At least 3 times the non-pipelined throughput at depth 16 | `redis-benchmark -P 16` |
| NFR-04 | Memory | Overhead per small key (16 B key, 64 B value) under 150 bytes beyond the payload | Measured with a 1 million key load and the allocator's stats |
| NFR-05 | Scalability | Remains correct with 1,000 concurrent connections | Load test script |
| NFR-06 | Correctness | No data races, leaks or undefined behaviour | ASan, UBSan and TSan on the full suite, run in CI |
| NFR-07 | Durability | With `everysec`, at most 1 second of acknowledged writes lost on `kill -9` | Automated crash test, 100 repetitions |
| NFR-08 | Recovery time | Startup with 1 million keys under 5 s from snapshot, under 15 s from AOF | Timed test |
| NFR-09 | Robustness | Parser survives 1 million fuzzed inputs and malformed-frame tests | libFuzzer or AFL++ run |
| NFR-10 | Security basics | Limits on request size and argument count; optional `requirepass` | Tests send oversized and unauthenticated requests |
| NFR-11 | Portability | Builds with GCC 11+ and Clang 14+ on Linux using CMake | CI matrix |
| NFR-12 | Maintainability | Core library under about 5,000 lines; each module has a header comment stating its invariants | Line count and code review |
| NFR-13 | Reproducibility | One command builds, one command tests, one script runs benchmarks | README walkthrough on a clean machine |

**Note on numbers.** The performance targets are starting hypotheses for a single-threaded or lightly threaded design on a laptop. The builder should measure first and then set final targets with evidence.

## 7. Architecture

&#91;embedded content: architecture · 5 layers, 1 thread owns the data\]

Requests flow down from the clients through the event loop, parser and dispatcher; only the event-loop thread touches the store, and write commands are also appended to the AOF. Replies go back up through the encoder. Source layout in section 14 mirrors these boxes one to one.

## 8. Data structures and algorithms

This section is the heart of the interview story. For each structure the builder writes down the alternative considered and why it lost.

| Need | Structure | Key design points | Complexity | Alternative considered |
| --- | --- | --- | --- | --- |
| Main keyspace | Hand-written hash table with separate chaining | Power-of-two bucket count; load factor limit 1.0; SipHash or FNV-1a hash; resize by incremental rehashing (move a few buckets per operation) so no single request stalls | Average O(1) get, set, delete; resize amortised | Open addressing: better cache use, harder deletion and resizing under load |
| Expiry index | Binary min-heap of (expire time, key) with lazy deletion | Heap entries hold a key handle and expiry stamp; an entry is skipped if the key's current expiry differs | O(log N) insert, O(log N) pop | Timing wheel: O(1) but coarser; a stretch comparison |
| Active expiry | Periodic pass popping due entries from the heap | Time-boxed to about 1 ms per tick so it cannot starve clients | Bounded work per tick | Random sampling as real Redis does |
| LRU eviction | Intrusive doubly linked list plus the hash table | Move-to-front on access; evict from the tail; node lives inside the entry to avoid extra allocation | O(1) touch, O(1) evict | Approximate LRU by sampling: less memory, less exact |
| Lists | Doubly linked list (or a deque of chunks) | Head and tail pointers; `LRANGE` walks from the nearer end | O(1) push and pop at both ends; O(N) range | Chunked array list: better cache locality, more code |
| Values | Tagged union of string, integer or list | Small-string optimisation; integers stored as 64-bit when the string is a valid integer | O(1) type check | `std::variant`: simpler, slightly larger |
| Input buffer | Ring buffer per connection | Handles partial reads; parser resumes where it stopped | O(1) amortised | Contiguous `std::string` with erase: simple but copies |
| Glob matching | Iterative matcher with backtracking limit | Supports `*`, `?` and character classes; guard against pathological patterns | O(N x M) worst case, bounded | Regex library: heavier and unnecessary |

**Invariants to document and assert in tests**

- Every key in the hash table appears in the LRU list exactly once, and the reverse.
- If a key has an expiry, it has at least one matching live heap entry.
- The count of keys, the sum of bucket sizes and the stored `size` field always agree.
- A key is never reachable after its expiry time has passed.

**Hash table details to decide in week 1**

1. Hash function and seed handling (fixed seed is fine for this project; note the hash-flooding trade-off in the README).
2. Bucket growth policy and the rehash step size per operation.
3. Memory ownership: the table owns key and value; nobody else stores raw pointers into entries that may move.

## 9. Persistence and recovery

Two mechanisms, as in real Redis: an append-only file for durability and a snapshot for fast restarts. Both are tested by deliberately killing the process.

**Append-only file (AOF)**

- Every write command that changes data is appended in RESP format, so the file can be replayed through the normal command path.
- Relative-expiry commands (`EXPIRE`, `SET ... EX`) are logged as absolute timestamps so replay after downtime does not extend a key's life.
- Fsync policy is configurable: `always` (safest, slowest), `everysec` (default), `no` (OS decides).
- Startup replays the file in order. A torn final record (a partial write from a crash) is detected and truncated with a logged warning; earlier records stay intact.
- Rewrite (compaction) writes a fresh AOF from the current dataset to a temporary file, then atomically renames it over the old one.

**Snapshot**

- A binary dump of all keys, values and expiries, with a header (magic, version) and a trailing checksum such as CRC32.
- Written to a temporary file, flushed, then renamed, so a crash never leaves a half-written snapshot as the live file.
- Taken on demand (`SAVE`) and optionally on a timer. A background variant through `fork()` is a stretch goal because it depends on copy-on-write behaviour the builder should first study.

**Startup order**

1. If an AOF exists, load it (it is the more complete source).
2. Otherwise load the snapshot, if present.
3. Drop keys whose expiry has already passed.
4. Begin accepting connections only after loading finishes, and log the key count and load time.

**Crash test plan**

| Test | Method | Expected result |
| --- | --- | --- |
| Kill during steady writes | A script writes sequence numbers, `kill -9` at a random moment, restart, read back | With `everysec`, missing writes are only from the last second; no gaps in the middle |
| Torn record | Truncate the AOF at random byte offsets | Server starts, truncates the bad tail, keeps earlier data |
| Corrupt snapshot | Flip random bytes in the snapshot | Checksum failure detected; server refuses it and falls back or reports clearly |
| Rewrite interrupted | Kill the server during AOF rewrite | Old AOF still valid; no data loss |
| TTL across restart | Set keys with TTLs, restart after they pass | Keys are gone after load |

## 10. Networking and concurrency design

**Decision: start with a single-threaded event loop, then add I/O threads only if measurements justify it.** Real Redis executes commands on one thread, which makes every command atomic without locks. Starting there removes a large class of bugs and lets the builder reach a correct server fast. Concurrency work then becomes an informed, measured step instead of a guess.

| Option | Description | Pros | Cons | Verdict |
| --- | --- | --- | --- | --- |
| A. Single thread, `epoll` loop | One thread accepts, reads, executes and writes using non-blocking sockets | Simple; commands are atomic; no locks; teaches `epoll` | One core only; slow commands block everyone | Build first (weeks 1 to 2) |
| B. Thread per connection | Each client gets a blocking thread | Easy to write | Needs locks around the store; poor scaling past a few hundred clients | Reject; document why |
| C. Event loop plus I/O thread pool | Worker threads parse and write; one thread executes commands | Uses more cores for I/O; keeps command atomicity | More moving parts | Stretch, only if profiling shows I/O is the bottleneck |
| D. Sharded store, one loop per shard | Keys hashed to shards; each shard single-threaded | Scales across cores | Multi-key commands become hard | Out of scope; mention in README |

**Event loop details**

- Non-blocking listening and client sockets, `epoll` in edge-triggered or level-triggered mode (choose one and document the reason).
- Per-connection state: input ring buffer, output queue, partial-parse position, last-activity time, authenticated flag.
- Read until `EAGAIN`, parse as many complete commands as the buffer holds (this is what makes pipelining work), execute them in order, then queue replies and write until `EAGAIN`; arm write interest only while output is pending.
- Backpressure: if a client's output queue exceeds a limit (for example 64 MB), close that client with a logged reason so one slow reader cannot exhaust memory.
- Timers: one monotonic-clock tick (for example every 100 ms) drives active expiry, idle-timeout checks and the AOF `everysec` fsync.
- Signal handling through `signalfd` or a self-pipe so shutdown runs on the loop thread.

**If threads are added later (option C)**

1. Keep a single owner for the keyspace; worker threads never touch it.
2. Hand off completed requests through a lock-free or mutex-protected queue; document the memory-ordering reasoning.
3. Run ThreadSanitizer on every test; any report blocks the merge.
4. Keep both modes behind a config flag and publish a before-and-after benchmark.

**Concurrency test ideas**

- 50 clients each increment the same counter 10,000 times; the final value must be exactly 500,000.
- Clients connect and disconnect randomly while others run commands; check for leaks and crashes.
- A deliberately slow client that never reads must not stall other clients.

## 11. Testing, benchmarking and validation

Testing is a deliverable, not an afterthought. The test suite and the benchmark report are the two artifacts an interviewer will trust most.

| Layer | What is tested | Tooling | Pass criteria |
| --- | --- | --- | --- |
| Unit | Hash table, heap, LRU list, ring buffer, glob matcher, RESP parser and encoder | GoogleTest (or Catch2) | All pass; at least 85% line coverage on the core library |
| Property and differential | Random sequences of `SET`, `GET`, `DEL`, `EXPIRE` applied to MiniRedis and to a `std::unordered_map` reference model; states must match after every step | Custom generator with a fixed seed printed on failure | 100,000 random runs with zero divergence |
| Protocol compatibility | Record real Redis replies for each supported command, then compare byte for byte | A script using `redis-cli` and `redis-py` against both servers | Identical replies on the supported list, including error messages for common mistakes |
| Fuzzing | Parser and command dispatcher | libFuzzer or AFL++ with ASan | 1 million inputs or 30 minutes without a finding, run before each milestone |
| Integration | Multiple clients, pipelining, large values, slow readers, idle timeout | Python test harness | All scenarios in section 10 pass |
| Durability | Crash and recovery scenarios from section 9 | Shell script plus Python verifier | All pass over 100 repetitions |
| Sanitizers | Memory errors, undefined behaviour, data races | ASan, UBSan, TSan builds in CI | Zero reports |
| Performance | Throughput, latency, memory per key | `redis-benchmark`, a custom load generator, `perf` for profiling | Reported with hardware, OS, compiler flags and command lines |

**Benchmark protocol**

1. Fix the environment: CPU model, core count, RAM, kernel, compiler and flags (`-O2`), and whether the machine is otherwise idle. Pin MiniRedis to one core when comparing designs.
2. Run each scenario at least five times; report median and spread, not the best run.
3. Scenarios: `SET` and `GET` at 1, 10, 50 and 500 clients; pipeline depth 1, 16 and 64; value sizes 16 B, 256 B and 4 KB; mixed 80% read and 20% write.
4. Compare against real Redis on the same machine, and say plainly that real Redis is expected to win; the point is to explain the gap with a profile.
5. Profile one slow case with `perf` and record what the top three hot spots are and what was changed.
6. Publish raw results (CSV), the scripts, and a short write-up in the repo.

**CI**

One workflow builds with GCC and Clang, runs unit and differential tests, the three sanitizer builds, and a short fuzz run on every push. A badge in the README shows status.

## 12. Milestones

&#91;embedded content: 4-week plan · 4 gates, final gate highlighted\]

Each gate is pass or fail. If a gate fails, the next week starts by fixing it, and the cut order in section 4 applies.

**Deliverables by week**

- **Week 1:** repo, CMake, CI with sanitizers; `epoll` echo server; RESP parser with split-at-every-byte tests; hash table with unit tests and the differential harness; `PING`, `SET`, `GET`, `DEL`, `EXISTS`. Gate 1: `redis-cli` runs all five commands correctly.
- **Week 2:** `EXPIRE` family, expiry heap with lazy and active expiry, `INCR` and `MSET` family, lists, AOF with fsync policies and replay. Gate 2: the crash test from section 9 passes 100 times in a row.
- **Week 3:** snapshot and AOF rewrite, pipelining, multi-client stress tests, parser fuzzing, coverage above 85%. Gate 3: the 50-client benchmark runs without errors and all sanitizer builds are clean.
- **Week 4:** LRU eviction, `INFO`, one profile-driven optimisation, benchmark report, README, design notes and demo video. Gate 4: every success criterion in section 2 is met.

## 13. Risks and mitigations

| # | Risk | Likelihood | Impact | Mitigation |
| --- | --- | --- | --- | --- |
| 1 | C++ gaps (ownership, undefined behaviour) slow the builder down | High | High | Use RAII and `std::unique_ptr` from day one; run sanitizers from week 1; keep modules small; review each module against a checklist |
| 2 | Scope creep into Redis features nobody will ask about | High | Medium | The cut order in section 4 is binding; new ideas go into a stretch list, not the sprint |
| 3 | `epoll` and non-blocking I/O bugs (partial reads, lost wakeups, busy loops) | Medium | High | Build the event loop first with an echo server; add tests that split requests at every byte boundary |
| 4 | Persistence bugs lose data silently | Medium | High | Differential tests, crash tests with `kill -9`, checksums, and a replay verifier |
| 5 | Performance misses the targets | Medium | Low | Targets are hypotheses; report measured numbers and a profile-based explanation instead |
| 6 | Time runs out before polish | Medium | High | Week 4 is reserved for tests, benchmarks and README; do not start stretch work until the week-3 gate passes |
| 7 | Relying on AI-written code the builder cannot explain | Medium | High | AI may explain concepts and review; the builder types the core modules and records design notes in their own words |
| 8 | Looks like a Redis clone with nothing new | Medium | Medium | Lead with the evidence: differential test results, crash-test results, and a profile-driven optimisation story |
| 9 | Environment problems (no Linux machine) | Low | Medium | Use WSL2 or a Linux container; CI also builds on Linux |

## 14. Repo, README, demo and resume packaging

**Repository layout**

```text
miniredis/
  CMakeLists.txt
  README.md
  docs/            design notes, architecture diagram, benchmark report
  src/
    net/           event loop, connection, buffers
    protocol/      RESP parser and encoder
    store/         hash table, expiry heap, LRU list, value types
    commands/      command table and handlers
    persist/       AOF and snapshot
    util/          logging, config, time
  tests/           unit, differential, integration, crash
  fuzz/            parser and dispatcher fuzz targets
  bench/           scripts and raw CSV results
  .github/workflows/ci.yml
```

**README must contain**

- One paragraph on what it is and what it is not (not production software).
- Build and run in three commands; a short `redis-cli` demo session.
- Supported command table and known deviations from real Redis.
- Architecture diagram and a link to the design-decisions document.
- Benchmark table with hardware details, plus the honest comparison to real Redis.
- Testing summary: coverage, sanitizers, fuzzing, crash tests.
- A "What I learned and what I would change" section.

**Design-decisions document (the interview script).** One page per decision, in the builder's own words: the problem, options considered, the choice, what was measured. Minimum list: hash table collision strategy, rehash policy, expiry structure, threading model, AOF format, eviction policy, memory ownership.

**Demo.** A two-minute screen recording: start the server, run commands in `redis-cli`, set a TTL and watch it expire, kill the server and restart to show recovery, run the benchmark. Link it at the top of the README.

**Resume bullets (fill with real measured numbers only)**

- Built a Redis-compatible in-memory key-value server in C++17 (RESP2, `epoll` event loop, custom hash table with incremental rehashing, TTL via min-heap, LRU eviction), verified with `redis-cli` and differential tests.
- Implemented AOF and snapshot persistence with checksums; validated recovery across 100 `kill -9` crash tests.
- Benchmarked at \[X\] ops/s with p99 \[Y\] ms; profiled with `perf` and cut \[Z\] by \[change\]; zero findings under ASan, UBSan and TSan.

**Interview questions the builder must be able to answer**

1. Why chaining and not open addressing? What happens on a hash collision attack?
2. How does incremental rehashing avoid a pause, and what breaks if you delete during a rehash?
3. Why is a single-threaded design reasonable, and when would it stop being enough?
4. What does `epoll` return, and what is the difference between level and edge triggering?
5. How does the AOF stay correct if the process dies mid-write?
6. Why log absolute expiry times instead of relative ones?
7. What did the profiler show, and what did you change because of it?

## 15. Assumptions, open questions and next steps

**Assumptions** (change any that are wrong and the plan adjusts)

- A1: The builder has a Linux machine, WSL2 or a Linux VM, with GCC or Clang and CMake installed.
- A2: The builder is comfortable with C and has basic C++ (classes, templates at a reading level). Smart pointers, RAII and move semantics are learned during week 1.
- A3: About 15 to 20 hours per week for four weeks.
- A4: The target is entry-level software or embedded roles, so correctness, tests and explainability matter more than raw speed.
- A5: Numbers in section 6 are starting hypotheses to be replaced by measurements.
- A6: Using real Redis for comparison and for recording expected replies is allowed and useful.

**Open questions**

- Q1: Is the target machine a laptop or a Linux server, and how many cores? (Changes benchmark design.)
- Q2: Should the project stay single-threaded, or must the I/O thread option be built to show concurrency? (Default: only if profiling justifies it.)
- Q3: Is a Docker image wanted for easy demos? (Default: yes, a small one.)
- Q4: Any preferred test framework or build system other than GoogleTest and CMake?

**First five actions**

1. Set up the repo, CMake, CI with sanitizers, and a hello-world test (day 1).
2. Write a single-threaded echo server with `epoll` and test it with `nc` (days 2 to 3).
3. Write the RESP parser with split-at-every-byte tests (days 3 to 5).
4. Implement the hash table with unit tests and the differential test harness (days 5 to 7).
5. Connect everything for `PING`, `SET`, `GET`, `DEL` and run `redis-cli` against it (end of week 1).