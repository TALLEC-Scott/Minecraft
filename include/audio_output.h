#pragma once

#include <miniaudio.h>

// Creates the game's single ma_engine and hooks it to the audio output.
//
// Desktop: a plain ma_engine_init (miniaudio picks the native backend).
//
// Web: miniaudio's own Web Audio backend uses a ScriptProcessorNode (main
// thread, 2048-frame buffers = ~85 ms of latency plus game-loop jitter), and
// its AudioWorklet backend blocks in emscripten_sleep during init (needs
// -sASYNCIFY). Instead the engine is created without a device and an
// AudioWorklet node (its own Wasm audio thread, 128-frame render quanta)
// pulls ma_engine_read_pcm_frames. The worklet starts asynchronously, so the
// engine is usable at once; sounds started before it is up are just mixed
// from the first quantum. If the worklet can't start, a ScriptProcessorNode
// drives the same engine instead. The AudioContext resumes on the first user
// gesture (pointerdown / keydown / touchend), as browsers require.
//
// The console gets one "[audio] backend=..." line once audio is running;
// with ?audiodebug in the URL each click also logs its start-to-mix delay.
namespace audio_output {

ma_result initEngine(ma_engine* engine);
// Stops pulling from the engine; call before ma_engine_uninit.
void shutdown();

// Latency probe (web, ?audiodebug only; no-ops otherwise): mark that a
// sound was just started; the audio callback timestamps the next mix.
void probeSoundStarted();
// Per-frame on the main thread: prints a pending probe result.
void pollDebug();

} // namespace audio_output
