// Tracy profiler shim. Include this instead of <tracy/Tracy.hpp> so the
// macros compile to nothing when BUILD_WITH_TRACY is off (TRACY_ENABLE
// is undefined). Keeps every translation unit free of ifdefs around the
// ZoneScoped / FrameMark calls themselves.
#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
// No-op replacements. Tracy already defines these as no-ops when
// TRACY_ENABLE isn't set IF the header is included — but we skip the
// header entirely in that case to avoid the dependency, so define the
// no-ops manually here.
#define ZoneScoped                                                                                                     \
    do {                                                                                                               \
    } while (0)
#define ZoneScopedN(name)                                                                                              \
    do {                                                                                                               \
    } while (0)
#define ZoneText(txt, len)                                                                                             \
    do {                                                                                                               \
    } while (0)
#define ZoneValue(val)                                                                                                 \
    do {                                                                                                               \
    } while (0)
#define FrameMark                                                                                                      \
    do {                                                                                                               \
    } while (0)
#define FrameMarkNamed(name)                                                                                           \
    do {                                                                                                               \
    } while (0)
#define TracyPlot(name, val)                                                                                           \
    do {                                                                                                               \
    } while (0)
#define TracyMessageL(msg)                                                                                             \
    do {                                                                                                               \
    } while (0)
#endif
