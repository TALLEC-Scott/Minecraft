#include "audio_output.h"

#ifndef __EMSCRIPTEN__

namespace audio_output {

ma_result initEngine(ma_engine* engine) {
    ma_engine_config config = ma_engine_config_init();
    return ma_engine_init(&config, engine);
}
void shutdown() {}
void probeSoundStarted() {}
void pollDebug() {}

} // namespace audio_output

#else

#include <algorithm>
#include <atomic>
#include <cstring>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/webaudio.h>

namespace {

constexpr int CHANNELS = 2;
// The ScriptProcessor fallback's buffer: 2048 frames (~43 ms) is what
// miniaudio's own ScriptProcessor path used; smaller underruns whenever a
// game frame runs long, since this callback shares the main thread.
constexpr int SCRIPT_PROCESSOR_FRAMES = 2048;
// Mix scratch: covers a whole ScriptProcessor buffer; worklet quanta (128)
// fit many times over, larger ones are mixed in chunks.
constexpr int MIX_FRAMES = SCRIPT_PROCESSOR_FRAMES;
constexpr int WORKLET_STACK_SIZE = 128 * 1024; // multiple of 16, as required

// Backend ids for the console report (decoded in JS).
constexpr int BACKEND_WORKLET = 1;
constexpr int BACKEND_SCRIPT_PROCESSOR = 2;

alignas(16) unsigned char g_workletStack[WORKLET_STACK_SIZE];
// Written only by whichever callback is live: the worklet, or (after the
// worklet failed) the ScriptProcessor on the main thread — never both.
float g_mix[MIX_FRAMES * CHANNELS];

EMSCRIPTEN_WEBAUDIO_T g_context = 0;
std::atomic<ma_engine*> g_engine{nullptr};
std::atomic<bool> g_rendering{false};

bool g_debug = false;
double g_probeStartMs = 0.0; // main thread only
std::atomic<bool> g_probeArmed{false};
std::atomic<bool> g_probeReady{false};
std::atomic<double> g_probeMixMs{0.0};

// Mixes `frames` interleaved stereo frames from the engine (silence once
// shut down). Runs on the audio worklet thread, or the main thread for the
// ScriptProcessor fallback.
void renderInterleaved(float* out, int frames) {
    g_rendering.store(true);
    ma_engine* engine = g_engine.load();
    if (engine == nullptr) {
        std::memset(out, 0, sizeof(float) * static_cast<size_t>(frames) * CHANNELS);
    } else {
        // Claimed before mixing: a sound started before the claim is in this mix.
        // Date.now on both sides: AudioWorkletGlobalScope has no performance.now.
        bool probe = g_probeArmed.load(std::memory_order_relaxed) && g_probeArmed.exchange(false);
        ma_engine_read_pcm_frames(engine, out, static_cast<ma_uint64>(frames), nullptr);
        if (probe) {
            g_probeMixMs.store(emscripten_date_now());
            g_probeReady.store(true);
        }
    }
    g_rendering.store(false);
}

bool processAudio(int /*numInputs*/, const AudioSampleFrame* /*inputs*/, int numOutputs, AudioSampleFrame* outputs,
                  int /*numParams*/, const AudioParamFrame* /*params*/, void* /*userData*/) {
    if (numOutputs < 1) return true;
    AudioSampleFrame& out = outputs[0];
    const int frames = out.samplesPerChannel;
    const int outChannels = out.numberOfChannels;
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, MIX_FRAMES);
        renderInterleaved(g_mix, n);
        // Worklet output is planar: data[channel * frames + i].
        for (int c = 0; c < outChannels; ++c) {
            const int src = std::min(c, CHANNELS - 1);
            float* dst = out.data + (static_cast<ptrdiff_t>(c) * frames) + done;
            for (int i = 0; i < n; ++i) dst[i] = g_mix[(i * CHANNELS) + src];
        }
        done += n;
    }
    return true;
}

// Logs the "[audio] backend=..." line once the context is running (it starts
// suspended until a user gesture; outputLatency is only meaningful after).
void reportWhenRunning(int backend, int periodFrames) {
    // clang-format off
    EM_ASM({
        var ctx = emscriptenGetAudioObject($0);
        var backend = $1 == 1 ? 'AudioWorklet' : 'ScriptProcessor';
        var period = $2;
        var ms = function(v) { return (typeof v === 'number') ? (v * 1000).toFixed(1) + 'ms' : 'n/a'; };
        var report = function() {
            console.log('[audio] backend=' + backend + ' rate=' + ctx.sampleRate + ' period=' + period + 'f (' +
                        (period * 1000 / ctx.sampleRate).toFixed(1) + 'ms) baseLatency=' + ms(ctx.baseLatency) +
                        ' outputLatency=' + ms(ctx.outputLatency));
        };
        if (ctx.state === 'running') { setTimeout(report, 300); return; }
        console.log('[audio] backend=' + backend + ' ready; AudioContext ' + ctx.state + ' until the first click/key');
        var onState = function() {
            if (ctx.state !== 'running') return;
            ctx.removeEventListener('statechange', onState);
            setTimeout(report, 300); // outputLatency reads 0 right after resume
        };
        ctx.addEventListener('statechange', onState);
    }, g_context, backend, periodFrames);
    // clang-format on
}

void startScriptProcessor(const char* why) {
    // clang-format off
    EM_ASM({
        console.warn('[audio] AudioWorklet unavailable (' + UTF8ToString($4) + '), using ScriptProcessorNode');
        var ctx = emscriptenGetAudioObject($0);
        var frames = $1;
        var channels = $2;
        var mixPtr = $3;
        var node = ctx.createScriptProcessor(frames, 0, channels);
        node.onaudioprocess = function(e) {
            _audio_output_render_script_processor(frames);
            var mix = HEAPF32.subarray(mixPtr >> 2, (mixPtr >> 2) + frames * channels);
            for (var c = 0; c < e.outputBuffer.numberOfChannels; c++) {
                var out = e.outputBuffer.getChannelData(c);
                var src = Math.min(c, channels - 1);
                for (var i = 0; i < frames; i++) out[i] = mix[i * channels + src];
            }
        };
        node.connect(ctx.destination);
        ctx.mcScriptNode = node; // keep it reachable
    }, g_context, SCRIPT_PROCESSOR_FRAMES, CHANNELS, g_mix, why);
    // clang-format on
    reportWhenRunning(BACKEND_SCRIPT_PROCESSOR, SCRIPT_PROCESSOR_FRAMES);
}

void onProcessorCreated(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* /*userData*/) {
    if (!success) {
        startScriptProcessor("processor creation failed");
        return;
    }
    int outputChannelCounts[1] = {CHANNELS};
    EmscriptenAudioWorkletNodeCreateOptions options{};
    options.numberOfInputs = 0;
    options.numberOfOutputs = 1;
    options.outputChannelCounts = outputChannelCounts;
    EMSCRIPTEN_AUDIO_WORKLET_NODE_T node =
        emscripten_create_wasm_audio_worklet_node(context, "mc-audio", &options, &processAudio, nullptr);
    emscripten_audio_node_connect(node, context, 0, 0);
    reportWhenRunning(BACKEND_WORKLET, emscripten_audio_context_quantum_size(context));
}

void onWorkletThreadStarted(EMSCRIPTEN_WEBAUDIO_T context, bool success, void* /*userData*/) {
    if (!success) {
        startScriptProcessor("worklet thread failed to start");
        return;
    }
    WebAudioWorkletProcessorCreateOptions options{};
    options.name = "mc-audio";
    emscripten_create_wasm_audio_worklet_processor_async(context, &options, &onProcessorCreated, nullptr);
}

// Browsers keep a new AudioContext suspended until a user gesture. Resume on
// press (pointerdown / keydown), not on click (mouse up) like miniaudio
// does, so the very first button click is already audible. Listeners stay
// on: Safari can re-suspend ("interrupted") and the check is cheap.
void installGestureResume() {
    // clang-format off
    EM_ASM({
        var ctx = emscriptenGetAudioObject($0);
        var resume = function() {
            if (ctx.state !== 'running' && ctx.state !== 'closed') ctx.resume().catch(function() {});
        };
        // (No array literal: a bare comma would split the EM_ASM macro arguments.)
        'pointerdown mousedown keydown touchend click'.split(' ').forEach(function(type) {
            window.addEventListener(type, resume, true);
        });
    }, g_context);
    // clang-format on
}

} // namespace

// Called by the ScriptProcessor fallback's onaudioprocess (main thread).
extern "C" EMSCRIPTEN_KEEPALIVE void audio_output_render_script_processor(int frames) {
    renderInterleaved(g_mix, std::min(frames, MIX_FRAMES));
}

namespace audio_output {

ma_result initEngine(ma_engine* engine) {
    // clang-format off
    g_debug = EM_ASM_INT({ return /[?&]audiodebug(=|&|$)/.test(location.search) ? 1 : 0; }) != 0;
    // AudioWorklets need a secure context; the Wasm audio thread needs SharedArrayBuffer.
    const bool worklets = EM_ASM_INT({
        return (typeof AudioWorkletNode !== 'undefined' && self.crossOriginIsolated === true &&
                window.isSecureContext === true) ? 1 : 0;
    }) != 0;
    // clang-format on

    EmscriptenWebAudioCreateAttributes attrs{};
    attrs.latencyHint = "interactive";
    attrs.sampleRate = 0; // the device's native rate: no resampling in the browser
    g_context = emscripten_create_audio_context(&attrs);

    // The engine runs at the context's rate and is pulled by our node, so it
    // needs no miniaudio device.
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = CHANNELS;
    config.sampleRate = static_cast<ma_uint32>(emscripten_audio_context_sample_rate(g_context));
    ma_result result = ma_engine_init(&config, engine);
    if (result != MA_SUCCESS) {
        emscripten_destroy_audio_context(g_context);
        g_context = 0;
        return result;
    }
    g_engine.store(engine);
    installGestureResume();

    if (worklets) {
        emscripten_start_wasm_audio_worklet_thread_async(g_context, g_workletStack, WORKLET_STACK_SIZE,
                                                         &onWorkletThreadStarted, nullptr);
    } else {
        startScriptProcessor("no AudioWorkletNode or not cross-origin isolated");
    }
    return MA_SUCCESS;
}

void shutdown() {
    if (g_context == 0) return;
    g_engine.store(nullptr);
    while (g_rendering.load()) {
        // A callback that loaded the engine before the store finishes its quantum.
    }
    emscripten_destroy_audio_context(g_context);
    g_context = 0;
}

void probeSoundStarted() {
    if (!g_debug) return;
    g_probeStartMs = emscripten_date_now();
    g_probeArmed.store(true);
}

void pollDebug() {
    if (!g_debug || !g_probeReady.load()) return;
    g_probeReady.store(false);
    const double startToMix = g_probeMixMs.load() - g_probeStartMs;
    // clang-format off
    EM_ASM({
        var ctx = emscriptenGetAudioObject($0);
        var out = ((ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000;
        console.log('[audio] click start→mix ' + $1.toFixed(1) + 'ms (+ base+output latency ' + out.toFixed(1) +
                    'ms ≈ ' + ($1 + out).toFixed(1) + 'ms to the speaker)');
    }, g_context, startToMix);
    // clang-format on
}

} // namespace audio_output

#endif
