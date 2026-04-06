#ifndef TEXTURE_H
#define TEXTURE_H

#include <iostream>
#include "gl_header.h"
#include <stb/stb_image.h>

class Texture {
  public:
    Texture();
    Texture(const char* texPath);

    unsigned int id;

    void bind();
    void destroy();

    ~Texture();
};

#endif
