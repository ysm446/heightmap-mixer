#pragma once

#include "rhi/Device.h"
#include "rhi/PipelineCache.h"

#include <DirectXMath.h>

#include <filesystem>
#include <string>

namespace mm::renderer {

// 手続き的な空の設定。単位は cd/m^2 相当。
// 太陽はディレクショナルライトで別に扱うため、ここには入れない
// （環境マップに入れると二重計上になり、解像度の都合でエイリアスも出る）。
struct SkySettings {
    DirectX::XMFLOAT3 zenithColor = {0.20f, 0.36f, 0.78f};
    DirectX::XMFLOAT3 horizonColor = {0.70f, 0.80f, 0.95f};
    // 地面は日射を受けて反射している想定。暗くしすぎると日中の露出で黒く潰れる。
    DirectX::XMFLOAT3 groundColor = {0.45f, 0.42f, 0.38f};
    // 晴天の空はおよそ 4000-15000 cd/m^2。日中の露出で見て自然になる値にしている。
    float intensity = 12000.0f;
};

// IBL 用の環境一式。
//   equirect → キューブマップ（ミップ付き）
//            → irradiance キューブ（拡散）
//            → プリフィルタ済みキューブ（鏡面、ラフネス別ミップ）
//   + 環境 BRDF の LUT
//
// 生成はすべてコンピュートで行い、Device::ExecuteImmediate でその場で完了させる。
class Environment {
public:
    bool Initialize(rhi::Device& device, rhi::PipelineCache& pipelineCache);
    void Shutdown(rhi::Device& device);

    // 手続き的な空から作り直す。
    bool BuildFromSky(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                      const SkySettings& sky);

    // Radiance HDR (.hdr) から作り直す。
    bool BuildFromHdrFile(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                          const std::filesystem::path& path);

    bool IsReady() const { return m_ready; }

    uint32_t EnvironmentSrvIndex() const { return m_cube.SrvIndex(); }
    uint32_t IrradianceSrvIndex() const { return m_irradiance.SrvIndex(); }
    uint32_t PrefilteredSrvIndex() const { return m_prefiltered.SrvIndex(); }
    uint32_t BrdfLutSrvIndex() const { return m_brdfLut.SrvIndex(); }
    uint32_t PrefilteredMipCount() const { return m_prefiltered.mipLevels; }

    const std::string& SourceName() const { return m_sourceName; }
    uint32_t EquirectWidth() const { return m_equirect.width; }
    uint32_t EquirectHeight() const { return m_equirect.height; }

private:
    bool CreateTargets(rhi::Device& device, uint32_t equirectWidth, uint32_t equirectHeight);
    void ReleaseTargets(rhi::Device& device);
    bool BuildBrdfLut(rhi::Device& device, rhi::PipelineCache& pipelineCache);

    // equirect が書き込まれた状態から、キューブ・irradiance・プリフィルタを作る。
    bool BuildFromEquirect(rhi::Device& device, rhi::PipelineCache& pipelineCache);

    rhi::GpuTexture m_equirect;
    rhi::GpuTexture m_cube;
    rhi::GpuTexture m_irradiance;
    rhi::GpuTexture m_prefiltered;
    rhi::GpuTexture m_brdfLut;

    std::string m_sourceName = "手続き的な空";
    bool m_ready = false;
};

}  // namespace mm::renderer
