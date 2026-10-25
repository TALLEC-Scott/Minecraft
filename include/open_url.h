#pragma once

// Opens a web page outside the game: a new browser tab on the web build
// (or the same tab if the browser blocks the popup), the default browser
// on desktop. `webPath` is relative to the game page; desktop builds have
// no page, so they open `desktopUrl` instead.
void openWebPage(const char* webPath, const char* desktopUrl);
