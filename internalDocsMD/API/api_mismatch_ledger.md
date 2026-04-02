# API Mismatch & Constraint Ledger
**Status:** Authoritative Hard-Constraint Appendix

This document catalogs structural realities that prohibit idealized API designs. **Do not attempt to refactor around these constraints without a fundamental audio-engine rewrite.**

1. **Staged Setup is Non-Negotiable:** The engine cannot be initialized with a monolithic `init()` struct. Object counts from `loadScene` dictate the memory allocations required before `applyLayout` can construct the spatial matrix. The 5-stage setup must remain.
2. **Restartable Stop/Seek is Unsafe:** The ring buffers and ADM block-streamers hold state that cannot be flushed atomically under the current architecture. Standard transport controls are limited strictly to `setPaused(bool)`.
3. **OSC Ownership:** `mParamServer` cannot be shared with the host application. It must be spun up and torn down inside `EngineSession` to guarantee valid AlloLib parameter scoping.
4. **Shutdown Sequence:** Violating the `mParamServer->stopServer()` -> `mBackend->shutdown()` -> `mStreaming->shutdown()` sequence **will** result in deadlocks on macOS CoreAudio and ASIO teardowns.
