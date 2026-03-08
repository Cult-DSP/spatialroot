# Streaming Agent

> **Implementation Status (2026-02-24):** The Streaming Agent is fully implemented in
> `spatial_engine/realtimeEngine/src/Streaming.hpp` with two input modes:
>
> 1. **Mono file mode** (`--sources`): Per-source `SourceStream` opens individual mono WAV
>    files. Background loader thread iterates sources independently.
> 2. **ADM direct streaming mode** (`--adm`): A shared `MultichannelReader` (in
>    `MultichannelReader.hpp`) opens one multichannel ADM WAV file, reads interleaved chunks,
>    and de-interleaves into per-source buffers. Eliminates stem splitting entirely.
>
> The design document below was written pre-implementation and is more generic/aspirational.
> See `Streaming.hpp`, `MultichannelReader.hpp`, and the Phase 2 + ADM Direct Streaming
> completion logs in `realtime_master.md` for the actual implementation details.

## Overview

The **Streaming Agent** is responsible for all audio input streaming and decoding in the real-time spatial audio engine. It handles the ingestion of audio data from various sources (such as audio files on disk, live streams, or network feeds) and prepares it for spatial rendering. This agent ensures that decoded audio frames are available to the audio processing pipeline **on time**, so that the Spatializer never runs out of data to render.

In practice, this means the Streaming Agent runs in its own thread (or threads) to read and decode audio in advance. It must manage buffers for each audio source and synchronize with the audio thread without blocking it. By isolating IO and decoding here, we prevent slow file or network operations from impacting the critical real-time audio callback.

## Responsibilities

- **Audio Decoding and Buffering:** Read audio data from input sources and decode it (if compressed) into raw PCM frames. This may involve using codecs (e.g., via FFmpeg or similar libraries) for formats like MP3, Ogg, etc. The agent should buffer a small queue of audio frames in memory for each active source.
- **Stream Management:** Handle multiple concurrent audio streams (sound sources). This includes opening/closing streams, managing file pointers or network sockets, and possibly performing sample rate conversion if an audio file’s rate differs from the engine’s rate.
- **Timely Delivery:** Ensure audio frames are delivered to the Spatializer (audio thread) just-in-time. The Streaming Agent should always aim to have at least a few blocks of audio ready ahead of the playback cursor. If a stream is too slow (e.g., network jitter), the agent might implement strategies like rebuffering or notifying the system of underrun risk.
- **Resource Management:** Pre-load or cache data when possible (for example, reading slightly ahead on disk) to avoid stalls. It should also manage memory for buffers prudently, recycling buffers once consumed by the audio thread, to avoid memory bloat.
- **Format Handling:** Potentially handle different channel formats per stream (mono, stereo, etc.) by converting them into a standard internal format for the Spatializer (e.g., mono sources for positioning, or if stereo source to possibly treat as two linked sources or downmix if needed). This agent will prepare the data in whatever form the spatial engine expects for input (likely mono per source in most spatialization scenarios).

## Relevant Internal Files

- **`AudioStream.cpp` / `AudioStream.h`:** This module (to be implemented) will likely contain the logic for reading from audio files or network streams. It could utilize third-party decoding libraries and manage the buffering.
- **`mainplayer.cpp`:** The main application may initialize and start the streaming threads via this agent. For example, `mainplayer.cpp` might call into the Streaming Agent to load a file or connect a stream, then periodically check for buffer underruns.
- **`SourceBufferManager.cpp`:** (If applicable) A helper that maintains circular buffers or queues for audio data per source. The Streaming Agent will interface with such a manager to push decoded frames.
- Other codec-specific files or library interfaces (e.g., `DecoderFFmpeg.cpp` if using FFmpeg) might also be part of this agent’s implementation, though those details depend on technology choices.

## Hard Real-Time Constraints

While the Streaming Agent operates outside the real-time audio callback, its behavior directly affects real-time performance:

- **Non-Blocking to Audio Thread:** The agent must **never block the audio thread**. Communication of audio data to the Spatializer should use lock-free queues or double-buffered mechanisms. For example, pushing data to a `LockFreeQueue<AudioBuffer>` that the audio thread pops from ensures no locking on the high-priority thread.
- **Timeliness:** If the Streaming Agent fails to provide data on time (e.g., due to a slow disk read or decode), the audio thread will underrun (silence or glitch). Therefore, this agent must decode ahead of playback. A common strategy is to maintain a target buffer fill level (e.g., keep 2–3 audio blocks queued) and wake up the streaming thread to refill when the level falls low.
- **Thread Priority and Yielding:** The streaming thread should run at a lower priority than the audio thread. However, it must still operate efficiently. If decoding is heavy, consider using multiple streaming threads for multiple sources (but avoid too many threads which could contend CPU). The thread can afford to block on I/O or decoding, but it should not hold onto shared resources needed by the audio thread while doing so.
- **Memory Management:** Allocate buffers ahead of time. For instance, if each audio frame (buffer) is a fixed size (like 128 or 256 samples per channel), allocate a pool of these buffers. Avoid allocating new memory for each decode, as memory allocation can be slow or unpredictable. Reuse buffers in a circular fashion.
- **Avoid Large Spikes:** The decoding process could be bursty (e.g., reading a large file chunk). The agent should pace its work to avoid scenarios where it monopolizes the CPU briefly and starves the audio thread. For example, decode one frame at a time then yield, rather than decoding a whole second of audio in one go without pause.

## Interaction with Other Agents

- **Spatializer (DBAP) Agent:** The Streaming Agent supplies the raw audio buffers for each source that the Spatializer will mix. Typically, the Spatializer will pull from a shared buffer (or the Streaming Agent will push data into a ring buffer that the Spatializer reads). The interface might be something like `AudioBuffer* StreamingAgent::getNextFrame(sourceID)` called by the audio thread, which must be lock-free/real-time safe.
- **Pose and Control Agent:** Indirectly, the Streaming Agent might respond to control commands such as _start/stop a source_ or _seek to a position_. Those commands could come from the Pose/Control agent (or directly from GUI through a control interface). For example, if a user triggers a sound or stops it, the control agent will instruct the Streaming Agent to open or close that stream. Thus, a thread-safe command queue or callback interface is needed between control logic and streaming.
- **Compensation and Gain Agent:** If there are per-stream gain adjustments (e.g., a source’s volume), the Streaming Agent itself might not apply these (that’s done in the mix), but it could, for instance, provide metadata or ensure the data is in a standard format for the gain agent to use. Also, if a stream has loudness metadata or requires normalization, the Streaming Agent could pass that info along so the gain agent can adjust (this is an edge consideration).
- **Threading and Safety:** The Streaming Agent heavily relies on the threading design. It uses the synchronization primitives or patterns defined by the Threading and Safety agent. For example, it may use a condition variable or atomic flag to signal when new data is available. All such interactions must follow the project’s lock-free guidelines to keep the audio thread safe.
- **GUI Agent:** Through the control agent or directly, the GUI might display streaming status (buffer fill level, playback position, etc.). The Streaming Agent should expose read-only status information in a thread-safe way. For instance, it could update atomic counters for how many buffers are ready, or what timestamp has been decoded up to, which the GUI can read and display.

## Data Structures & Interfaces

- **Audio Buffer Queues:** A likely structure is a per-source ring buffer or queue (e.g., `std::vector<AudioBuffer>` in a circular setup). Each `AudioBuffer` might contain a small block of PCM samples. The Streaming Agent pushes decoded buffers into this queue; the Spatializer pops from it during each callback. The queue should be lock-free or use a single-producer single-consumer model to avoid locks (the Streaming Agent is producer, audio thread is consumer).
- **Source Descriptor:** There may be a structure representing each audio source (e.g., `AudioSource` or `StreamHandle`) containing information like file handle or stream socket, current read position, and the pointer to its buffer queue. The Streaming Agent manages a collection of these source descriptors, possibly in a map or list, protected by a mutex (only accessed outside audio thread).
- **Decode Thread Interface:** The agent might spawn a thread for each source or a pool of threads for decoding. An alternative is a single thread handling all sources in round-robin. In either case, the interface could be an update loop that checks which source needs data (if its buffer is running low) and then decodes the next chunk for that source.
- **Lock-Free Signals:** Use atomic flags or ring buffer write indices to signal data availability. For example, an atomic index for “next frame available to read” per source can be incremented by the Streaming thread and read by the audio thread.
- **Error Handling Interface:** If a stream underruns (can’t supply data in time) or an error occurs (file read error), the Streaming Agent should have a way to report this. Possibly it can set a flag that the audio thread or main thread checks, or send a message via the control agent to pause or mute that source. This ensures graceful handling instead of crashing the audio thread.

## Development and Documentation Notes

The developer implementing the Streaming Agent should:

- **Update this document** as design decisions are made (e.g., confirming which buffering strategy or decoding library is used).
- **Coordinate with Spatializer and Threading docs:** Ensure the assumptions about buffer sizes and timing align with the Spatializer’s needs. If the Spatializer expects a certain frame size or format, document that here and adhere to it.
- **Record Progress:** Update the master plan (`realtime_master.md`) with any changes in how streaming is handled that affect the overall architecture (for example, if switching from single-threaded to multi-threaded decoding, note it in the master doc). Also update `internalDocsMD/agents.md` to reflect the status of the Streaming Agent (e.g., when basic streaming is working, or if any issues are encountered).
- **RENDERING.md Implications:** If the audio rendering pipeline assumptions change due to streaming (for instance, supporting a new audio format or introducing latency buffers), document those in the overall RENDERING.md. Generally, RENDERING.md might include how sources are integrated into rendering, so ensure any relevant details (like latency of buffering or format conversions) are noted.
- **Testing:** As part of development notes, plan to test the Streaming Agent under stress (multiple streams, high bitrates) and document results. This helps reassure that real-time constraints are met. Include any important observations (e.g., “Decoding 10 MP3 streams simultaneously uses 40% CPU, which is within budget”) in this doc or a testing section.

By following these guidelines and keeping documentation up-to-date, the Streaming Agent will provide a solid foundation for audio data, allowing the rest of the engine to focus on spatial processing without worrying about where the next audio sample is coming from.
