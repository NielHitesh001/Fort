# Fort Development Roadmap

This roadmap outlines planned future research, architectural enhancements, and engineering milestones for the Fort simulation framework.

---

## Milestone 1: Protocol & Network Expansion
- [ ] **SBE (Simple Binary Encoding) Decoder**: Add CME MDP 3.0 market data feed parser.
- [ ] **ITCH 5.0 Multicast Live Capture**: Direct pcap/PCAP-NG offline file replay harness with nanosecond timestamp pacing.
- [ ] **Solarflare OpenOnload / EF_VI Backend**: Hardware kernel-bypass integration for Solarflare Onload network adapters.

## Milestone 2: Algorithmic & AI Enhancements
- [ ] **Continuous Reinforcement Learning Execution**: Deep Q-Network (DQN) / PPO execution agent interacting with the simulated LOB.
- [ ] **GPU-Accelerated Monte Carlo Option Pricer**: CUDA / Metal kernels for multi-asset stochastic volatility jump-diffusion simulation.
- [ ] **Dynamic Hidden Liquidity Detection**: Hawkes process estimator for iceberg and dark pool hidden reserve replenishment.

## Milestone 3: Distributed Clustering & Formal Verification
- [ ] **TLA+ Formal Specification**: Formal specification and model checking of the Multi-Region Raft cross-DC consensus algorithm.
- [ ] **Zero-Copy FlatBuffers / Protocol Buffers IPC**: Shared-memory inter-process communication bus between matching engine and risk controller.
- [ ] **Prometheus Exporter Daemon**: Standalone background metrics bridge exporting lock-free ring buffer telemetry to Prometheus format.

---

*Note: Items on this roadmap are intended for educational research and algorithmic exploration.*
