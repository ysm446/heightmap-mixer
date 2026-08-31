#include "core/ImageIo.h"

#include "core/Log.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// tinyexr は miniz を同梱の実装から使う。vcpkg 版はライブラリとして提供されるので、
// ここでは実装マクロを定義しない。
#include <tinyexr.h>

namespace mm {

bool LoadLdrImage(const std::filesystem::path& path, LdrImage& outImage) {
    outImage = LdrImage{};

    const std::string utf8Path = path.string();

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data = ::stbi_load(utf8Path.c_str(), &width, &height, &channels, 4);
    if (data == nullptr) {
        MM_LOG_ERROR("画像を読み込めません: %s (%s)", utf8Path.c_str(), ::stbi_failure_reason());
        return false;
    }

    outImage.width = static_cast<uint32_t>(width);
    outImage.height = static_cast<uint32_t>(height);
    outImage.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
    ::stbi_image_free(data);

    MM_LOG_INFO("画像を読み込みました: %s (%d x %d, %d ch)", utf8Path.c_str(), width, height,
                channels);
    return true;
}

bool LoadHdrImage(const std::filesystem::path& path, HdrImage& outImage) {
    outImage = HdrImage{};

    const std::string utf8Path = path.string();

    int width = 0;
    int height = 0;
    int channels = 0;
    float* data = ::stbi_loadf(utf8Path.c_str(), &width, &height, &channels, 4);
    if (data == nullptr) {
        MM_LOG_ERROR("HDR 画像を読み込めません: %s (%s)", utf8Path.c_str(), ::stbi_failure_reason());
        return false;
    }

    outImage.width = static_cast<uint32_t>(width);
    outImage.height = static_cast<uint32_t>(height);
    outImage.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
    ::stbi_image_free(data);

    MM_LOG_INFO("HDR 画像を読み込みました: %s (%d x %d, %d ch)", utf8Path.c_str(), width, height,
                channels);
    return true;
}

bool LoadExrImage(const std::filesystem::path& path, HdrImage& outImage) {
    outImage = HdrImage{};

    const std::string utf8Path = path.string();

    float* data = nullptr;
    int width = 0;
    int height = 0;
    const char* error = nullptr;
    // LoadEXR は常に RGBA の 4 チャンネルで返す。
    const int result = ::LoadEXR(&data, &width, &height, utf8Path.c_str(), &error);
    if (result != TINYEXR_SUCCESS) {
        MM_LOG_ERROR("EXR を読み込めません: %s (%s)", utf8Path.c_str(),
                     (error != nullptr) ? error : "原因不明");
        if (error != nullptr) {
            ::FreeEXRErrorMessage(error);
        }
        return false;
    }

    outImage.width = static_cast<uint32_t>(width);
    outImage.height = static_cast<uint32_t>(height);
    outImage.pixels.assign(data, data + static_cast<size_t>(width) * height * 4);
    ::free(data);

    MM_LOG_INFO("EXR を読み込みました: %s (%d x %d)", utf8Path.c_str(), width, height);
    return true;
}

bool SaveRgba8Png(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t rowPitch, const uint8_t* pixels) {
    if (pixels == nullptr || width == 0 || height == 0) {
        return false;
    }

    const std::string utf8Path = path.string();
    const int result = ::stbi_write_png(utf8Path.c_str(), static_cast<int>(width),
                                        static_cast<int>(height), 4, pixels,
                                        static_cast<int>(rowPitch));
    if (result == 0) {
        MM_LOG_ERROR("PNG を書き出せません: %s", utf8Path.c_str());
        return false;
    }

    MM_LOG_INFO("PNG を書き出しました: %s (%u x %u)", utf8Path.c_str(), width, height);
    return true;
}

bool SaveGray8Png(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t rowPitch, const uint8_t* pixels) {
    if (pixels == nullptr || width == 0 || height == 0) {
        return false;
    }

    const std::string utf8Path = path.string();
    const int result = ::stbi_write_png(utf8Path.c_str(), static_cast<int>(width),
                                        static_cast<int>(height), 1, pixels,
                                        static_cast<int>(rowPitch));
    if (result == 0) {
        MM_LOG_ERROR("PNG を書き出せません: %s", utf8Path.c_str());
        return false;
    }

    MM_LOG_INFO("PNG を書き出しました: %s (%u x %u, 1 ch)", utf8Path.c_str(), width, height);
    return true;
}

}  // namespace mm
