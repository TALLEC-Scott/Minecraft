// Force-included via -include before every TracyClient source file when
// cross-compiling for Windows with mingw-w64. Fixes two breakages:
//
// 1. TracySocket.cpp pulls in <winsock2.h> after something else has
//    already included <windows.h>, which on mingw headers means
//    WSAPoll / struct pollfd / POLLIN don't end up declared. Ensure
//    winsock2.h is seen first, with WIN32_LEAN_AND_MEAN set so the
//    later windows.h doesn't re-enter winsock.h.
//
// 2. TracyProfiler.cpp calls __get_cpuid() expecting it from <cpuid.h>,
//    but <cpuid.h> conflicts with mingw's <intrin.h> (__cpuid macro
//    collision). Provide an inline asm shim instead.

#ifndef TRACY_MINGW_PREFIX_H
#define TRACY_MINGW_PREFIX_H

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

#if defined(__MINGW32__) && !defined(__get_cpuid)
static inline int __get_cpuid(unsigned int leaf, unsigned int* eax,
                              unsigned int* ebx, unsigned int* ecx, unsigned int* edx) {
    unsigned int a, b, c, d;
    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "0"(leaf), "2"(0));
    *eax = a; *ebx = b; *ecx = c; *edx = d;
    return 1;
}
#endif

#endif // TRACY_MINGW_PREFIX_H
