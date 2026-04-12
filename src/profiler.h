#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "gl_header.h"
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <cstdio>
#endif

// Sample the current process's resident memory (bytes). Returns 0 if unsupported.
inline size_t sampleProcessRss() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.WorkingSetSize;
    return 0;
#elif defined(__linux__)
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    size_t rss = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "VmRSS: %zu kB", &rss) == 1) { rss *= 1024; break; }
    }
    std::fclose(f);
    return rss;
#else
    return 0;
#endif
}

// Per-frame accumulators — set by render/buildMesh, read by profiler at end of frame.
// Reset at the start of each frame.
struct FrameAccum {
    double meshBuildMs = 0;
    int meshBuilds = 0;
    int meshBuildBudget = 4; // max mesh builds per frame
    int opaqueTriangles = 0;
    int waterTriangles = 0;
    int opaqueDrawCalls = 0;
    int waterDrawCalls = 0;
    int chunksRendered = 0;
    int vertexCount = 0;
    // Total CPU-side memory of all loaded chunks (sum of Chunk::memoryUsage).
    // Sampled by ChunkManager::update each frame. Preserved across reset()
    // since it's a state snapshot, not a per-frame accumulator.
    size_t chunkMemoryBytes = 0;
    // Per-component breakdown for debugging memory bottlenecks.
    size_t memSectionsBytes = 0;
    size_t memSkyLightBytes = 0;
    size_t memWaterLevelsBytes = 0;
    size_t memMeshCacheBytes = 0;
    size_t memPendingMeshBytes = 0;
    // Process RSS (resident set size) in bytes, sampled OS-side.
    size_t processRssBytes = 0;

    void reset() {
        size_t keepChunk = chunkMemoryBytes;
        size_t keepSec = memSectionsBytes;
        size_t keepSky = memSkyLightBytes;
        size_t keepWater = memWaterLevelsBytes;
        size_t keepMesh = memMeshCacheBytes;
        size_t keepPending = memPendingMeshBytes;
        size_t keepRss = processRssBytes;
        *this = {};
        meshBuildBudget = 4;
        chunkMemoryBytes = keepChunk;
        memSectionsBytes = keepSec;
        memSkyLightBytes = keepSky;
        memWaterLevelsBytes = keepWater;
        memMeshCacheBytes = keepMesh;
        memPendingMeshBytes = keepPending;
        processRssBytes = keepRss;
    }
};

// Global accumulator — cheaply written to from hot paths
inline FrameAccum g_frame;

struct FrameProfile {
    double totalMs = 0;
    double updateMs = 0;
    double renderMs = 0;
    double swapMs = 0;

    // From g_frame accumulators
    double meshBuildMs = 0;
    int meshBuilds = 0;
    int opaqueTriangles = 0;
    int waterTriangles = 0;
    int opaqueDrawCalls = 0;
    int waterDrawCalls = 0;
    int chunksRendered = 0;
    int chunksTotal = 0;
    int vertexCount = 0;

    // GPU timer query result
    double gpuMs = 0;
};

class Profiler {
  public:
    bool gpuTimerSupported = false;

    void init() {
#ifndef __EMSCRIPTEN__
        GLint bits = 0;
        glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
        gpuTimerSupported = (bits > 0);
        if (gpuTimerSupported) {
            glGenQueries(2, gpuQueries);
        }
#endif

        // Log renderer info
        renderer = (const char*)glGetString(GL_RENDERER);
        vendor = (const char*)glGetString(GL_VENDOR);
        version = (const char*)glGetString(GL_VERSION);
    }

    void destroy() {
#ifndef __EMSCRIPTEN__
        if (gpuTimerSupported) glDeleteQueries(2, gpuQueries);
#endif
    }

    void beginFrame() {
        g_frame.reset();
        frameStart = now();
    }

    void beginUpdate() {
        updateStart = now();
    }
    void endUpdate() {
        cur.updateMs = ms(updateStart);
    }

    void beginRender() {
        renderStart = now();
#ifndef __EMSCRIPTEN__
        if (gpuTimerSupported) glBeginQuery(GL_TIME_ELAPSED, gpuQueries[queryIdx]);
#endif
    }
    void endRender() {
#ifndef __EMSCRIPTEN__
        if (gpuTimerSupported) glEndQuery(GL_TIME_ELAPSED);
#endif
        cur.renderMs = ms(renderStart);
    }

    void beginSwap() {
        swapStart = now();
    }
    void endSwap() {
        cur.swapMs = ms(swapStart);
    }

    void endFrame(int chunksTotal) {
        cur.totalMs = ms(frameStart);

#ifndef __EMSCRIPTEN__
        // Read GPU timer (blocks until result ready — fine for profiling)
        if (gpuTimerSupported) {
            GLuint64 gpuNs = 0;
            glGetQueryObjectui64v(gpuQueries[queryIdx], GL_QUERY_RESULT, &gpuNs);
            cur.gpuMs = gpuNs / 1e6;
            queryIdx = 1 - queryIdx; // alternate queries
        }
#endif

        // Copy accumulators
        cur.meshBuildMs = g_frame.meshBuildMs;
        cur.meshBuilds = g_frame.meshBuilds;
        cur.opaqueTriangles = g_frame.opaqueTriangles;
        cur.waterTriangles = g_frame.waterTriangles;
        cur.opaqueDrawCalls = g_frame.opaqueDrawCalls;
        cur.waterDrawCalls = g_frame.waterDrawCalls;
        cur.chunksRendered = g_frame.chunksRendered;
        cur.chunksTotal = chunksTotal;
        cur.vertexCount = g_frame.vertexCount;

        frames.push_back(cur);
        cur = {};
    }

    // Also collect the old-style frame time (render+swap only) for backward compat
    void recordLegacyFrameTime(double ms) {
        legacyFrameTimes.push_back(ms);
    }

    void report(const std::string& label) const {
        if (frames.empty()) return;
        int n = (int)frames.size();

        auto percentile = [](std::vector<double> v, double p) {
            std::sort(v.begin(), v.end());
            return v[(size_t)(v.size() * p)];
        };

        auto avg = [](const std::vector<double>& v) { return std::accumulate(v.begin(), v.end(), 0.0) / v.size(); };

        // Extract per-metric vectors
        std::vector<double> total, update, render, swap, meshBuild, gpu;
        std::vector<int> meshBuilds, opaqueTri, waterTri, drawCalls, chunks, verts;
        for (auto& f : frames) {
            total.push_back(f.totalMs);
            update.push_back(f.updateMs);
            render.push_back(f.renderMs);
            swap.push_back(f.swapMs);
            meshBuild.push_back(f.meshBuildMs);
            gpu.push_back(f.gpuMs);
            meshBuilds.push_back(f.meshBuilds);
            opaqueTri.push_back(f.opaqueTriangles);
            waterTri.push_back(f.waterTriangles);
            drawCalls.push_back(f.opaqueDrawCalls + f.waterDrawCalls);
            chunks.push_back(f.chunksRendered);
            verts.push_back(f.vertexCount);
        }

        double avgTotal = avg(total);
        double avgUpdate = avg(update);
        double avgRender = avg(render);
        double avgSwap = avg(swap);
        double avgMeshBuild = avg(meshBuild);
        double avgGpu = avg(gpu);

        double avgMeshBuilds = std::accumulate(meshBuilds.begin(), meshBuilds.end(), 0.0) / n;
        double avgOpaqueTri = std::accumulate(opaqueTri.begin(), opaqueTri.end(), 0.0) / n;
        double avgWaterTri = std::accumulate(waterTri.begin(), waterTri.end(), 0.0) / n;
        double avgDrawCalls = std::accumulate(drawCalls.begin(), drawCalls.end(), 0.0) / n;
        double avgChunks = std::accumulate(chunks.begin(), chunks.end(), 0.0) / n;
        double avgVerts = std::accumulate(verts.begin(), verts.end(), 0.0) / n;
        int avgTotal_chunks = frames.back().chunksTotal;

        auto pct = [](double part, double whole) { return whole > 0 ? part / whole * 100 : 0; };

        std::ostringstream out;
        out << std::fixed << std::setprecision(2);

        out << "\n=== Profile: " << label << " ===\n";
        out << renderer << " (" << vendor << ")\n";
        out << version << "\n";
        out << n << " measured frames\n\n";

        out << "Frame Time Breakdown (avg):\n";
        out << "  total:     " << avgTotal << " ms  (" << (int)(1000 / avgTotal) << " fps)\n";
        out << "  update:    " << avgUpdate << " ms  [" << pct(avgUpdate, avgTotal) << "%]\n";
        out << "  render:    " << avgRender << " ms  [" << pct(avgRender, avgTotal) << "%]  <- includes mesh builds\n";
        out << "  swap:      " << avgSwap << " ms  [" << pct(avgSwap, avgTotal) << "%]\n\n";

        out << "Render Breakdown (avg):\n";
        out << "  mesh builds:  " << avgMeshBuild << " ms  [" << pct(avgMeshBuild, avgRender) << "% of render]";
        out << "  (" << avgMeshBuilds << " builds/frame)\n";
        out << "  draw calls:   " << (avgRender - avgMeshBuild) << " ms  [" << pct(avgRender - avgMeshBuild, avgRender)
            << "% of render]";
        out << "  (" << avgDrawCalls << " calls/frame)\n\n";

        if (gpuTimerSupported) {
            out << "GPU Time:\n";
            out << "  avg:  " << avgGpu << " ms";
            if (avgGpu < avgRender * 0.5)
                out << "  <- CPU-bound (GPU idle " << (int)(100 - pct(avgGpu, avgRender)) << "% of render)";
            else if (avgGpu > avgRender * 0.9)
                out << "  <- GPU-bound";
            out << "\n";
            out << "  p99:  " << percentile(gpu, 0.99) << " ms\n\n";
        }

        out << "Geometry (avg per frame):\n";
        out << "  chunks:    " << (int)avgChunks << " rendered / " << avgTotal_chunks << " loaded\n";
        out << "  triangles: " << (int)avgOpaqueTri << " opaque + " << (int)avgWaterTri
            << " water = " << (int)(avgOpaqueTri + avgWaterTri) << " total\n";
        out << "  vertices:  " << (int)avgVerts << "\n\n";

        if (g_frame.chunkMemoryBytes > 0 || g_frame.processRssBytes > 0) {
            out << "Memory:\n";
            auto mb = [](size_t b) { return b / (1024 * 1024); };
            if (g_frame.chunkMemoryBytes > 0) {
                out << "  chunks (CPU): " << mb(g_frame.chunkMemoryBytes) << " MB\n";
                out << "    sections:    " << mb(g_frame.memSectionsBytes) << " MB\n";
                out << "    skyLight:    " << mb(g_frame.memSkyLightBytes) << " MB\n";
                out << "    waterLevels: " << mb(g_frame.memWaterLevelsBytes) << " MB\n";
                out << "    meshCache:   " << mb(g_frame.memMeshCacheBytes) << " MB\n";
                out << "    pendingMesh: " << mb(g_frame.memPendingMeshBytes) << " MB\n";
            }
            if (g_frame.processRssBytes > 0)
                out << "  process RSS:  " << mb(g_frame.processRssBytes) << " MB\n";
            out << "\n";
        }

        out << "Percentiles (ms):\n";
        out << "  total:  avg=" << avg(total) << "  p50=" << percentile(total, 0.50)
            << "  p95=" << percentile(total, 0.95) << "  p99=" << percentile(total, 0.99)
            << "  max=" << *std::max_element(total.begin(), total.end()) << "\n";
        out << "  render: avg=" << avg(render) << "  p50=" << percentile(render, 0.50)
            << "  p95=" << percentile(render, 0.95) << "  p99=" << percentile(render, 0.99)
            << "  max=" << *std::max_element(render.begin(), render.end()) << "\n";
        if (gpuTimerSupported)
            out << "  gpu:    avg=" << avg(gpu) << "  p50=" << percentile(gpu, 0.50)
                << "  p95=" << percentile(gpu, 0.95) << "  p99=" << percentile(gpu, 0.99)
                << "  max=" << *std::max_element(gpu.begin(), gpu.end()) << "\n";

        std::string report = out.str();
        std::cout << report;

        // Write to profile file
        std::ofstream file("profile_results.txt");
        file << report;
        file.close();
    }

    // Write backward-compatible benchmark_results.txt
    void writeLegacyResults(int measureFrames) const {
        if (legacyFrameTimes.empty()) return;
        double sum = 0, minT = legacyFrameTimes[0], maxT = legacyFrameTimes[0];
        for (double t : legacyFrameTimes) {
            sum += t;
            minT = std::min(minT, t);
            maxT = std::max(maxT, t);
        }
        double avgT = sum / legacyFrameTimes.size();
        auto sorted = legacyFrameTimes;
        std::sort(sorted.begin(), sorted.end());
        double p99 = sorted[(size_t)(sorted.size() * 0.99)];

        std::ofstream out("benchmark_results.txt");
        out << std::fixed << std::setprecision(2);
        out << "frames:  " << measureFrames << "\n";
        out << "avg:     " << avgT << " ms  (" << (int)(1000.0 / avgT) << " fps)\n";
        out << "min:     " << minT << " ms  (" << (int)(1000.0 / minT) << " fps)\n";
        out << "max:     " << maxT << " ms  (" << (int)(1000.0 / maxT) << " fps)\n";
        out << "p99:     " << p99 << " ms  (" << (int)(1000.0 / p99) << " fps)\n";
        out.close();
    }

  private:
    double now() {
        return glfwGetTime();
    }
    double ms(double start) {
        return (glfwGetTime() - start) * 1000.0;
    }

#ifndef __EMSCRIPTEN__
    GLuint gpuQueries[2] = {};
    int queryIdx = 0;
#endif
    double frameStart = 0, updateStart = 0, renderStart = 0, swapStart = 0;
    FrameProfile cur;
    std::vector<FrameProfile> frames;
    std::vector<double> legacyFrameTimes;

    std::string renderer, vendor, version;
};
