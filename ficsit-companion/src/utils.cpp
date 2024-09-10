#include <algorithm>
#include <filesystem>
#include <map>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include "utils.hpp"

unsigned int DefaultTexture()
{
    const size_t height = 64;
    const size_t width = 64;
    std::vector<unsigned char> texture_data(4 * width * height, 0);
    for (size_t row = 0; row < height; ++row)
    {
        for (size_t col = 0; col < width; ++col)
        {
            const size_t pixel_index = (row * width + col) * 4;
            // Set alpha to 255
            texture_data[pixel_index + 3] = 255;
            // If top left or bottom right corner, set RGB to magenta
            if ((row < height / 2 && col < width / 2) || (row > height / 2 - 1 && col > width / 2 - 1))
            {
                texture_data[pixel_index + 0] = 255;
                texture_data[pixel_index + 2] = 255;
            }
        }
    }

    // Create OpenGL texture
    GLuint image_index;
    glGenTextures(1, &image_index);
    glBindTexture(GL_TEXTURE_2D, image_index);

    // Setup filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels to texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data.data());

    return image_index;
}

unsigned int LoadTextureFromFile(const std::string& path)
{
    static unsigned int default_texture = DefaultTexture();
    static std::map<std::string, unsigned int> cached_textures;

    auto it = cached_textures.find(path);
    if (it != cached_textures.end())
    {
        return it->second;
    }

    if (!std::filesystem::exists(path))
    {
        cached_textures[path] = default_texture;
        return default_texture;
    }

    int width = 0;
    int height = 0;
    unsigned char* image = stbi_load(path.c_str(), &width, &height, NULL, 4);

    if (image == NULL)
    {
        cached_textures[path] = default_texture;
        return default_texture;
    }

    // Create OpenGL texture
    GLuint image_index;
    glGenTextures(1, &image_index);
    glBindTexture(GL_TEXTURE_2D, image_index);

    // Setup filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels to texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    stbi_image_free(image);

    cached_textures[path] = image_index;
    return image_index;
}

bool UpdateSave(Json::Value& save, const int to)
{
    if (save["save_version"].get<int>() == to)
    {
        return true;
    }

    // No backward support
    if (save["save_version"].get<int>() > to)
    {
        return false;
    }

    // From 1 to 2, remove all "is_out" from pins as they are now directional
    if (save["save_version"].get<int>() == 1)
    {
        for (auto& l : save["links"].get_array())
        {
            l["start"].get_object().erase("is_out");
            l["end"].get_object().erase("is_out");
        }

        save["save_version"] = 2;
    }

    if (save["save_version"].get<int>() == to)
    {
        return true;
    }

    // From 2 to 3, add num_somersloop to nodes
    if (save["save_version"].get<int>() == 2)
    {
        for (auto& n : save["nodes"].get_array())
        {
            n["num_somersloop"] = 0;
        }

        save["save_version"] = 3;
    }

    if (save["save_version"].get<int>() == to)
    {
        return true;
    }

    return false;
}
