#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "splash.h"

splash_img img;

splash_img get_splashscreen() {
    img.buf = stbi_load("app0:app_splash.png", &img.w, &img.h, &img.n, 4);
    return img;
}
