#pragma once

#include "compositor/MaterialEvaluator.h"
#include "compositor/PaintMask.h"
#include "compositor/TextureLibrary.h"
#include "renderer/Camera.h"
#include "renderer/Environment.h"
#include "renderer/Mesh.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"

#include <DirectXMath.h>

namespace mm::renderer {

enum class PreviewShape {
    Sphere,
    Plane,
    Cube,
};

enum class TonemapMode : uint32_t {
    None = 0,
    Reinhard = 1,
    Aces = 2,
};

// 物理カメラの露出設定。
//   EV100    = log2(N^2 / t) - log2(ISO / 100)
//   exposure = 1 / (1.2 * 2^EV100)
struct ExposureSettings {
    bool useManualEv = false;
    float manualEv100 = 15.0f;
    float aperture = 16.0f;              // N（F 値）
    float shutterSpeed = 1.0f / 250.0f;  // t（秒）
    float iso = 100.0f;

    float Ev100() const;
    float Exposure() const;
};

struct LightSettings {
    float azimuth = 0.9f;               // 方位角（ラジアン）
    float elevation = 0.9f;             // 仰角（ラジアン）
    float illuminance = 100000.0f;      // lux。晴天の直射日光がおよそ 100000
    DirectX::XMFLOAT3 color = {1.0f, 0.98f, 0.95f};

    // サーフェスから光源へ向かう正規化ベクトル。
    DirectX::XMFLOAT3 Direction() const;
};

struct MaterialSettings {
    DirectX::XMFLOAT3 baseColor = {0.82f, 0.80f, 0.78f};
    float roughness = 0.35f;
    float metallic = 0.0f;
};

// シーンを HDR で描き、露出とトーンマップを通して表示用テクスチャへ書き出す。
class PreviewRenderer {
public:
    bool Initialize(rhi::Device& device, rhi::PipelineCache& pipelineCache);
    void Shutdown(rhi::Device& device);

    // 環境マップの作り直しは GPU 待機を伴うため、フレームの外で呼ぶこと。
    void RequestSkyRebuild() { m_skyRebuildRequested = true; }
    void RequestHdrLoad(const std::filesystem::path& path) { m_pendingHdrPath = path; }
    // 環境マップやマテリアル解像度の作り直しは GPU 待機を伴うため、
    // フレームの外でまとめて処理する。
    void ProcessPendingWork(rhi::Device& device, rhi::PipelineCache& pipelineCache);

    // 表示先のサイズに合わせてレンダーターゲットを作り直す。
    bool Resize(rhi::Device& device, uint32_t width, uint32_t height);

    void Render(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                ID3D12GraphicsCommandList* commandList, const compositor::MaterialStack& stack,
                const compositor::TextureLibrary& textures,
                const compositor::MaterialLibrary& materials,
                const compositor::PaintMaskStore& paintMasks);

    // ペイントのブラシパスが UV バッファを読むための準備をする。
    // フレーム内、Render より前に呼ぶこと（読むのは前フレームの内容）。
    compositor::PaintContext PrepareUvBufferForRead(ID3D12GraphicsCommandList* commandList);

    Camera& GetCamera() { return m_camera; }
    ExposureSettings& Exposure() { return m_exposure; }
    LightSettings& Light() { return m_light; }
    MaterialSettings& Material() { return m_material; }
    PreviewShape& Shape() { return m_shape; }
    TonemapMode& Tonemap() { return m_tonemap; }
    SkySettings& Sky() { return m_sky; }
    const Environment& GetEnvironment() const { return m_environment; }
    // 読み込み済みの HDRI のパス。手続き的な空を使っているときは空。
    const std::filesystem::path& HdriPath() const { return m_hdriPath; }
    float& IblIntensity() { return m_iblIntensity; }
    bool& ShowSkybox() { return m_showSkybox; }
    bool& UseMaterialTextures() { return m_useMaterialTextures; }
    float& MaterialUvScale() { return m_materialUvScale; }
    const compositor::MaterialEvaluator& Evaluator() const { return m_evaluator; }
    uint32_t MaterialResolution() const { return m_materialResolution; }
    void RequestMaterialResolution(uint32_t resolution) { m_requestedMaterialResolution = resolution; }

    // 表示用テクスチャを PNG に書き出す。フレームの外で呼ぶこと。
    bool SaveOutputToPng(rhi::Device& device, const std::filesystem::path& path);

    bool HasOutput() const { return m_output.IsValid(); }
    D3D12_GPU_DESCRIPTOR_HANDLE OutputHandle() const { return m_output.srv.gpu; }
    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

private:
    const Mesh& CurrentMesh() const;
    void ReleaseTargets(rhi::Device& device);

    Mesh m_sphere;
    Mesh m_plane;
    Mesh m_cube;

    rhi::GpuTexture m_sceneColor;  // 線形 HDR
    // メッシュ描画の 2 枚目のターゲット。xy: マテリアル UV、z: メッシュに当たったか。
    // ビューポートのカーソル位置からマテリアルの UV を引くために使う。
    rhi::GpuTexture m_materialUv;
    rhi::GpuTexture m_depth;
    rhi::GpuTexture m_output;  // トーンマップ後の表示用

    Camera m_camera;
    ExposureSettings m_exposure;
    LightSettings m_light;
    MaterialSettings m_material;
    Environment m_environment;
    compositor::MaterialEvaluator m_evaluator;
    SkySettings m_sky;
    PreviewShape m_shape = PreviewShape::Sphere;
    TonemapMode m_tonemap = TonemapMode::Aces;
    float m_iblIntensity = 1.0f;
    float m_materialUvScale = 1.0f;
    uint32_t m_materialResolution = 1024;
    uint32_t m_requestedMaterialResolution = 1024;
    bool m_useMaterialTextures = true;
    bool m_showSkybox = true;
    bool m_skyRebuildRequested = false;
    std::filesystem::path m_pendingHdrPath;
    std::filesystem::path m_hdriPath;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

}  // namespace mm::renderer
