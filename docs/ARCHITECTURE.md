# Fort System Architecture & Design

This document details the software architecture, memory layout, concurrency model, and data flow pipelines of Fort.

---

## 1. High-Level Data Flow

```
+-------------------------------------------------------------------------+
|                              Inbound Feeds                              |
|           (Nasdaq ITCH 5.0 / MoldUDP64 / Nanosecond PCAP Replay)        |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                  Fast Protocol Decoder & Sanitizer                      |
|           (Bounds checking, message validation, binary unpack)          |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                 In-Memory Limit Order Book (LOB) Engine                 |
|       (Price-Time FIFO, Iceberg, Pegged, STP/MMP, Auction Imbalance)    |
+-------------------------------------------------------------------------+
                                    |
                                    +-----------------------+
                                    |                       |
                                    v                       v
+---------------------------------------+   +-----------------------------+
|    Microstructure Feature Pipeline    |   |     Order Routing / Risk    |
| (OFI, Microprice, Tree ML Inference)  |   | (SEC 201, Reg NMS, Collars) |
+---------------------------------------+   +-----------------------------+
                                    |                       |
                                    +-----------+-----------+
                                                |
                                                v
+-------------------------------------------------------------------------+
|                  Lock-Free SPSC Telemetry & WORM Audit                  |
|                 (Nanosecond metrics, SHA-256 hash chain)                |
+-------------------------------------------------------------------------+
```

---

## 2. Memory Architecture & Cache Locality

1. **Pre-Allocated Memory Arenas (`luv_arena.hpp`)**:
   - The entire working memory for order nodes, price levels, and ring buffers is allocated at startup.
   - Eliminates page faults and kernel heap allocation locks (`malloc`/`free`) during critical processing.
2. **64-Byte Cache Line Alignment (`alignas(64)`)**:
   - Critical data structures (such as `OrderNode`, `PriceLevel`, `RaftLogEntry`, `FeatureRow`) are aligned to 64-byte boundaries.
   - Prevents CPU false sharing across worker cores.

---

## 3. Threading & Concurrency Model

- **Single-Writer Principle**:
  - The market data parser and LOB mutating engine operate as a single-threaded writer, avoiding mutex lock contention.
- **Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffers**:
  - Communication between the matching engine, risk validator, and telemetry export is handled via atomic head/tail ring buffers with acquire/release memory semantics (`std::memory_order_acquire` / `std::memory_order_release`).
- **Failure Modes & Safety**:
  - Memory exhaustion triggers immediate deterministic error return (`nullptr` / `false`) rather than undefined behavior.
  - Multi-tiered circuit breakers and price collars halt order generation if feed anomalies or extreme spread volatility are detected.
