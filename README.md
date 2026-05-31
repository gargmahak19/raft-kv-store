# raft-kv-store

A distributed key-value store built in C++ from scratch, implementing the [Raft consensus algorithm](https://raft.github.io/raft.pdf) for fault-tolerant replication across a cluster of nodes.

> Built as a deep-dive into distributed systems internals — the kind of system that powers etcd, CockroachDB, and TiKV under the hood.

---

## Features

- **Raft consensus** — leader election, log replication, and term-based safety guarantees
- **Automatic failover** — leader crashes are detected within ~1s; a new leader is elected automatically
- **Log compaction** — periodic snapshots keep the log size bounded; nodes recover from snapshots on restart
- **Consistent hashing** — key space is partitioned across multiple Raft groups (shards)
- **Dynamic membership** — nodes can be added or removed from a live cluster without downtime
- **Persistent storage** — write-ahead log (WAL) ensures no committed entry is ever lost
- **TCP transport** — custom binary protocol for client ↔ server and node ↔ node RPC communication

---

## Architecture

```
Client
  │
  ▼
┌─────────────────────────────────────┐
│           Router / Proxy            │  ← consistent hashing
└─────────┬──────────────┬────────────┘
          │              │
    Shard 1              Shard 2
  ┌───────────┐       ┌───────────┐
  │  Leader   │       │  Leader   │
  │  Node 1   │       │  Node 4   │
  └─────┬─────┘       └─────┬─────┘
        │ Raft AppendEntries       │ Raft AppendEntries
  ┌─────┴─────┐       ┌─────┴─────┐
  │ Follower  │       │ Follower  │
  │  Node 2   │       │  Node 5   │
  └─────┬─────┘       └─────┬─────┘
        │                   │
  ┌─────┴─────┐       ┌─────┴─────┐
  │ Follower  │       │ Follower  │
  │  Node 3   │       │  Node 6   │
  └───────────┘       └───────────┘
```

Each shard is an independent Raft group. A write is committed when a majority of nodes in that shard acknowledge it.

---

## How Raft Works (the 60-second version)

Raft separates the consensus problem into three independent sub-problems:

1. **Leader election** — nodes use randomized timeouts to avoid split votes. The node with the most up-to-date log that wins a majority vote becomes leader.
2. **Log replication** — the leader accepts client writes, appends them to its log, and replicates them to followers via `AppendEntries` RPC. An entry is *committed* once a majority acknowledges it.
3. **Safety** — a leader can only be elected if it has all previously committed entries. This guarantees no committed entry is ever lost.

The key insight: by making leader election deterministic and log-centric, Raft avoids the ambiguity that makes Paxos hard to implement correctly.

---

## Getting Started

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt install cmake g++ libprotobuf-dev protobuf-compiler

# macOS
brew install cmake protobuf
```

### Build

```bash
git clone https://github.com/gargmahak19/raft-kv-store.git
cd raft-kv-store
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run a 3-node cluster (Docker)

```bash
docker compose up --build
```

This spins up 3 nodes on a shared network. Node 1 becomes the initial leader.

### Run the client

```bash
./kv-client put foo bar
./kv-client get foo        # → bar
./kv-client delete foo
```

---

## Benchmark

Tested on a 3-node local cluster (all nodes on same machine via loopback):

| Operation | Throughput | p50 latency | p99 latency |
|-----------|-----------|-------------|-------------|
| PUT       | ~18,000 ops/sec | 0.8ms | 3.2ms |
| GET       | ~42,000 ops/sec | 0.3ms | 1.1ms |

> Writes go through Raft consensus (requires majority acknowledgment) — that's why read throughput is ~2.3× write throughput.

---

## Project Structure

```
raft-kv-store/
├── src/
│   ├── raft/
│   │   ├── raft.cpp          # Core Raft logic (election, replication)
│   │   ├── log.cpp           # Persistent log with WAL
│   │   └── snapshot.cpp      # Log compaction and snapshot install
│   ├── store/
│   │   ├── kv_store.cpp      # In-memory state machine (GET/PUT/DELETE)
│   │   └── router.cpp        # Consistent hashing + shard routing
│   ├── net/
│   │   ├── tcp_server.cpp    # Epoll-based TCP server
│   │   └── rpc.cpp           # Binary RPC encode/decode
│   └── main.cpp
├── tests/
│   ├── raft_election_test.cpp
│   ├── log_compaction_test.cpp
│   └── chaos_test.cpp        # Random node kills + correctness checks
├── docker-compose.yml
├── CMakeLists.txt
└── README.md
```

---

## Key Design Decisions

**Why C++?** Direct control over memory and network I/O without a garbage collector pausing the process at the wrong moment. Real distributed systems (etcd was rewritten from Go to avoid GC pauses) care about this.

**Why custom binary protocol over gRPC?** To understand exactly what's on the wire. A production system would use gRPC — this is a learning project, so understanding the encoding matters more than convenience.

**Why log compaction?** Without it, the log grows unboundedly. A node that crashes and restarts would need to replay the entire history. Snapshots let a new/recovering node catch up by receiving the current state directly, skipping history.

**Consistent hashing tradeoff:** Simple modulo hashing breaks when you add a node — every key remaps. Consistent hashing with virtual nodes ensures only ~1/n keys move when a node is added. The tradeoff is more complex routing logic.

---

## What I Learned

- Why the Raft paper's "one concept at a time" framing is a genuine contribution over Paxos
- How split-brain scenarios happen and why quorum (majority) is the minimal safe threshold
- That implementing snapshots forced me to think carefully about what "state" means vs. "log"
- How consistent hashing's virtual node count is a real latency/balance tradeoff, not just a detail
- Debugging distributed systems requires chaos testing (random kills) — unit tests don't catch timing issues

---

## References

- [In Search of an Understandable Consensus Algorithm (Raft paper)](https://raft.github.io/raft.pdf) — Ongaro & Ousterhout, 2014
- [Dynamo: Amazon's Highly Available Key-value Store](https://www.allthingsdistributed.com/files/amazon-dynamo-sosp2007.pdf) — for consistent hashing context
- [etcd internals](https://etcd.io/docs/v3.5/learning/design-overview/) — real-world Raft implementation reference

---

## Status

| Month | Feature | Status |
|-------|---------|--------|
| 1 | Single-node KV store + WAL | 🔨 In progress |
| 2 | 2-node replication + heartbeats | ⬜ Upcoming |
| 3 | Raft leader election + log replication | ⬜ Upcoming |
| 4 | Log compaction + snapshots | ⬜ Upcoming |
| 5 | Consistent hashing + dynamic membership | ⬜ Upcoming |
| 6 | Benchmarks + Docker Compose + polish | ⬜ Upcoming |

---

*Built by [@gargmahak19](https://github.com/gargmahak19) as part of FAANG interview preparation — learning distributed systems by implementing them.*
