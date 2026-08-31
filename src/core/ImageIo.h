#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace mm {

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
// HDRI の「空」の代表輝度（上側の中央値）。
//
// **HDRI は絶対輝度で較正されていない。** 較正するには、画像のどこかを
// 「これは何 cd/m^2 か」と決める必要がある。上空は写真ごとの差が小さく、
// 晴天なら 1 万 cd/m^2 前後という手掛かりがあるため、そこを基準にする。
//
// 太陽に引きずられないよう平均ではなく中央値を取る（最大は空の 2000 倍にもなる）。
float MedianSkyLuminance(const HdrImage& image);

bool LoadHdrImage(const std::filesystem::path& path, HdrImage& outImage);

// OpenEXR (.exr) を読み込む。失敗したら false を返し、理由はログへ出す。
// Megascans のテクスチャは EXR で配られることが多い。
bool LoadExrImage(const std::filesystem::path& path, HdrImage& outImage);

// RGBA8 のピクセル列を PNG として保存する。rowPitch はバイト単位。
bool SaveRgba8Png(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t rowPitch, const uint8_t* pixels);

// 1 チャンネル 8bit のピクセル列を PNG として保存する。ペイントマスクの保存に使う。
bool SaveGray8Png(const std::filesystem::path& path, uint32_t width, uint32_t height,
                  uint32_t rowPitch, const uint8_t* pixels);

}  // namespace mm
