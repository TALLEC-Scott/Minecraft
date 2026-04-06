#pragma once

#include <string>

#ifdef __EMSCRIPTEN__
inline std::string patchShaderForES(const std::string& src, bool isFragment) {
    std::string patched = src;
    auto pos = patched.find("#version 330 core");
    if (pos != std::string::npos) {
        std::string header = "#version 300 es\n";
        if (isFragment) header += "precision highp float;\nprecision highp sampler2DArray;\n";
        patched.replace(pos, 17, header);
    }
    return patched;
}
#endif
