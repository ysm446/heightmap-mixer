#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace hm {

// 線形 HDR 画像。常に RGBA の 4 チャンネルで保持する。
struct HdrImage {
    std::vector<float> pixels;
    uint32_t width = 0;
    uint32_t height = 0;

    bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }
    size_t RowPitchInBytes() const { return static_cast<size_t>(width) * 4 * sizeof(float); }
};

// 8bit の LDR 画像。常に RGBA の 4 チャンネルで保持する。
struct LdrImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;

    bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }
    size_t RowPitchInBytes() const { return static_cast<size_t>(width) * 4; }
};

// PNG / TGA / JPG などを読み込む。失敗したら false を返し、理由はログへ出す。
bool LoadLdrImage(const std::filesystem::path& path, LdrImage& outImage);

// Radiance HDR (.hdr) を読み込む。失敗したら false を返し、理由はログへ出す。
bool LoadHdrImage(const std::filesystem::path& path, HdrImage& outImage);

// RGBA8 のピクセル列を PNG として保存する。rowPitch はバイト単位。
bool SaveRgba8Png(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t rowPitch, const uint8_t* pixels);

}  // namespace hm
