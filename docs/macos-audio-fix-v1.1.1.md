# macOS audio stability — v1.1.1 (38)

The old audio queue refilled its buffers on the main run loop. UI/Metal work could therefore starve playback. A regression test that blocks the UI for 300 ms reproduced zero refilled buffers and zero source frames consumed. The installed 64-frame preference provided only 4 ms of queued PCM at 48 kHz. Moving callbacks off the UI thread alone still failed the playback-progress and idle-tail checks at that size.

The fix uses Core Audio’s internal callback thread, at least 256 frames per buffer (three buffers = 16 ms), and serializes queue start/stop/rebuild against keyboard playback. Older 64/128-frame preferences resolve to 256. The settings picker matches the supported range. The app no longer changes the shared hardware buffer size.

Concurrent voices are bounded at 32. Recycled silent buffers are always cleared. Obsolete device retries cannot reset a recovered queue. PCM stays at 48 kHz across device changes, with custom packs resampled on load. Variable-length output-device AudioBufferList storage is sized from Core Audio instead of allocating only one buffer.

Validation on a MacBook Pro with built-in speaker output, 2026-10-30:

- 14 tests passed, including all 10 existing fireworks/HDR/route tests.
- With the main thread blocked for 300 ms, more than 20 audio buffers and 8,000 source frames continue to advance on the independent callback thread.
- 500 immediate triggers stay within 32 voices and drain; three subsequent idle-stop/restart cycles play and drain successfully.
- Simultaneous playback and queue rebuilds complete without stale queue use; audio continues afterward.
- A 100 ms 44.1 kHz fixture converts to 4,800 frames at 48 kHz.

These checks exercise the real local Core Audio output path. They do not constitute human listening confirmation or a guarantee against all system-wide overloads. Windows audio code is unchanged.

API reference: [Apple AudioQueueNewOutput](https://developer.apple.com/documentation/audiotoolbox/audioqueuenewoutput(_:_:_:_:_:_:_:)). A null callback run loop uses an internal audio-queue thread.
