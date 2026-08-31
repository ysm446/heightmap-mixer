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

// ビューポートに何を出すか。シェーダの MM_VIEW_* と一致させること。
//
// シェーディング結果以外は「チャンネルの中身をそのまま見る」ための表示で、
// 露出もトーンマップも掛けない。
enum class DebugView : uint32_t {
    Shaded = 0,
    BaseColor = 1,
    // 法線マップそのもの（接空間）と、陰影に使う向き（ワールド空間）。
    NormalTangent = 2,
    NormalWorld = 3,
    Roughness = 4,
    Metallic = 5,
    AmbientOcclusion = 6,
    Height = 7,
    // 形だけを見る表示。ラスタライザをワイヤーフレームにする。
    Wireframe = 8,
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
// プレビュー設定の既定値。**メンバ初期化子・UI の既定値マーカー・
// プロジェクト読み込みのフォールバックの 3 か所で必ずこれを使う。**
// 数値を直接書くと、片方だけ変えたときに「既定値マーカーが点いたまま」
// 「読み込みで別の値に化ける」という食い違いが起きる。
struct PreviewDefaults {
    PreviewShape shape = PreviewShape::Sphere;
    TonemapMode tonemap = TonemapMode::Aces;
    bool useMaterialTextures = true;
    float materialUvScale = 1.0f;
    float displacementScale = 0.0f;
    bool tessellationEnabled = false;
    float tessellationFactor = 8.0f;
    uint32_t materialResolution = 1024;
    float iblIntensity = 1.0f;
    // 晴天の空はおよそ 4000-15000 cd/m^2。SkySettings::intensity と揃えてある。
    float hdriSkyLuminance = 12000.0f;
    bool showSkybox = true;
    bool skyboxBlur = false;
    bool shadowEnabled = true;
};
inline constexpr PreviewDefaults kPreviewDefaults{};

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
    // 現在のメッシュを包む球の半径（原点中心）。カメラの Frame() が使う。
    float BoundingRadius() const;
    TonemapMode& Tonemap() { return m_tonemap; }
    DebugView& Debug() { return m_debugView; }
    DebugView Debug() const { return m_debugView; }
    SkySettings& Sky() { return m_sky; }
    const Environment& GetEnvironment() const { return m_environment; }
    // 読み込み済みの HDRI のパス。手続き的な空を使っているときは空。
    const std::filesystem::path& HdriPath() const { return m_hdriPath; }
    float& IblIntensity() { return m_iblIntensity; }
    // **この HDRI の空を何 cd/m^2 とみなすか。** HDRI は絶対輝度で較正されて
    // いないので、基準をここで与える。晴天の空がおよそ 1 万。
    float& HdriSkyLuminance() { return m_hdriSkyLuminance; }
    float HdriSkyLuminance() const { return m_hdriSkyLuminance; }
    // 目標輝度だけを変えて環境を作り直す。ファイルは読み直さない。
    void RequestHdriLuminanceRebuild() { m_hdriLuminanceRebuildRequested = true; }
    bool& ShowSkybox() { return m_showSkybox; }
    // 背景だけをぼかす。**IBL の寄与は変えない。**
    // プリフィルタ済みキューブの粗いミップを引くだけなので、追加のパスは要らない。
    bool& SkyboxBlur() { return m_skyboxBlur; }
    // ディレクショナルライトの影を落とすか。落とさないとシャドウパスも走らない。
    bool& ShadowEnabled() { return m_shadowEnabled; }
    // テセレーション（画面上の辺の長さに応じた分割）を使うか。
    bool& TessellationEnabled() { return m_tessellationEnabled; }
    // 1 辺あたりの分割の上限。
    float& TessellationFactor() { return m_tessellationFactor; }
    bool& UseMaterialTextures() { return m_useMaterialTextures; }
    float& MaterialUvScale() { return m_materialUvScale; }
    // ハイトを形状に反映する量（0 で反映しない）。頂点シェーダで押し出す。
    float& DisplacementScale() { return m_displacementScale; }
    float DisplacementScale() const { return m_displacementScale; }
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
    // ライトから見たビュー×投影。プレビューの被写体を囲む平行投影。
    DirectX::XMMATRIX LightViewProjection() const;
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
    // ディレクショナルライトから見た深度。ビューポートの大きさとは無関係に固定。
    rhi::GpuTexture m_shadowMap;

    Camera m_camera;
    ExposureSettings m_exposure;
    LightSettings m_light;
    MaterialSettings m_material;
    Environment m_environment;
    compositor::MaterialEvaluator m_evaluator;
    SkySettings m_sky;
    PreviewShape m_shape = kPreviewDefaults.shape;
    TonemapMode m_tonemap = kPreviewDefaults.tonemap;
    DebugView m_debugView = DebugView::Shaded;
    float m_iblIntensity = kPreviewDefaults.iblIntensity;
    float m_hdriSkyLuminance = kPreviewDefaults.hdriSkyLuminance;
    bool m_hdriLuminanceRebuildRequested = false;
    float m_materialUvScale = kPreviewDefaults.materialUvScale;
    float m_displacementScale = kPreviewDefaults.displacementScale;
    uint32_t m_materialResolution = kPreviewDefaults.materialResolution;
    uint32_t m_requestedMaterialResolution = kPreviewDefaults.materialResolution;
    bool m_useMaterialTextures = kPreviewDefaults.useMaterialTextures;
    bool m_showSkybox = kPreviewDefaults.showSkybox;
    bool m_skyboxBlur = kPreviewDefaults.skyboxBlur;
    bool m_shadowEnabled = kPreviewDefaults.shadowEnabled;
    bool m_tessellationEnabled = kPreviewDefaults.tessellationEnabled;
    float m_tessellationFactor = kPreviewDefaults.tessellationFactor;
    bool m_skyRebuildRequested = false;
    std::filesystem::path m_pendingHdrPath;
    std::filesystem::path m_hdriPath;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

}  // namespace mm::renderer
