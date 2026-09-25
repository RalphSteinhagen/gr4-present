// the implementation macro emits definitions wherever it is set, so exactly one translation unit may set it
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
