#include "open_url.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#elif defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/types.h>
extern char** environ;
#endif

void openWebPage(const char* webPath, const char* desktopUrl) {
#ifdef __EMSCRIPTEN__
    (void)desktopUrl;
    // Runs a frame after the click, still within the browser's
    // user-activation window, so the popup is normally allowed.
    EM_ASM(
        {
            var path = UTF8ToString($0);
            var tab = window.open(path, '_blank');
            if (tab)
                tab.opener = null;
            else
                location.href = path;
        },
        webPath);
#elif defined(_WIN32)
    (void)webPath;
    ShellExecuteA(nullptr, "open", desktopUrl, nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)webPath;
    char* argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(desktopUrl), nullptr};
    pid_t pid = 0;
    posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ);
#endif
}
