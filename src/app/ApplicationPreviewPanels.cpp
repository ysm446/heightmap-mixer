// プレビュー設定パネルと「ライティング」パネル。
// どちらも合成結果ではなく、見え方（レンダラ側の設定）を扱う。

#include "app/Application.h"

#include "app/ApplicationUiHelpers.h"
#include "core/FileDialog.h"
#include "core/Log.h"
#include "io/ProjectIo.h"
#include "ui/UiStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace mm {

void Application::DrawMaterialPanel() {
    // **ここでは前面を要求しない。** レイヤーと同じ枠のタブなので、
    // 両方が要求すると後から描いたほうが勝ち、既定の前面が定まらない。
    if (ImGui::Begin("プレビュー設定")) {
        if (ui::BeginPropertyTable("previewRows")) {
            static const char* const kShapeLabels[] = {"球", "平面", "キューブ"};
            const renderer::PreviewDefaults& defaults = renderer::kPreviewDefaults;
            int shape = static_cast<int>(m_renderer.Shape());
            if (ui::PropertyCombo("形状", &shape, kShapeLabels, IM_ARRAYSIZE(kShapeLabels),
                                  static_cast<int>(defaults.shape),
                                  "マテリアルを貼って確かめるメッシュ")) {
                m_renderer.Shape() = static_cast<renderer::PreviewShape>(shape);
            }

            ui::PropertyBool("合成結果", &m_renderer.UseMaterialTextures(),
                             defaults.useMaterialTextures,
                             "オフにすると、レイヤー合成を使わず単色マテリアルで表示する");

            if (m_renderer.UseMaterialTextures()) {
                ui::PropertyFloat("UV スケール", &m_renderer.MaterialUvScale(), 0.25f, 8.0f,
                                  defaults.materialUvScale,
                                  "マテリアルをメッシュ上に何回並べるか", "%.2f", 0, 0.25f);

                ui::PropertyFloat("変位量", &m_renderer.DisplacementScale(), 0.0f, 0.5f,
                                  defaults.displacementScale,
                                  "ハイトを形状に反映する量（ディスプレイスメント）。"
                                  "0 なら形は変わらない",
                                  "%.2f", 0, 0.01f);

                ui::PropertyBool("テセレーション", &m_renderer.TessellationEnabled(),
                                 defaults.tessellationEnabled,
                                 "画面上の辺が長いところだけメッシュを細かく割る。"
                                 "変位量を上げたときに形がなめらかになる");
                if (m_renderer.TessellationEnabled()) {
                    ui::PropertyFloat("分割の上限", &m_renderer.TessellationFactor(), 1.0f, 16.0f,
                                      defaults.tessellationFactor,
                                      "1 辺をこの回数まで割る。上げるほど重くなる",
                                      "%.0f", 0, 1.0f);
                }

                int resolution = ResolutionIndex(m_renderer.MaterialResolution());
                if (ui::PropertyCombo("合成解像度", &resolution, kResolutionLabels,
                                      IM_ARRAYSIZE(kResolutionLabels),
                                      ResolutionIndex(defaults.materialResolution),
                                      "編集中のプレビュー解像度。上げるほど細部が出るが重くなる")) {
                    m_renderer.RequestMaterialResolution(kResolutionValues[resolution]);
                }
            } else {
                renderer::MaterialSettings& material = m_renderer.Material();
                ui::PropertyColorLinear("ベースカラー", &material.baseColor.x,
                                        &kDefaultMaterial.baseColor.x);
                ui::PropertyFloat("ラフネス", &material.roughness, 0.0f, 1.0f,
                                  kDefaultMaterial.roughness, nullptr, "%.2f");
                ui::PropertyFloat("メタルネス", &material.metallic, 0.0f, 1.0f,
                                  kDefaultMaterial.metallic, nullptr, "%.2f");
            }
            ui::EndPropertyTable();
        }

        ui::SectionHeader("カメラ");
        if (ui::BeginPropertyTable("cameraRows")) {
            // 露出を絞り / シャッター / ISO で決めているので、レンズも同じ言葉で扱う。
            // ラジアンのままだと何 mm 相当なのか分からない。
            renderer::Camera& camera = m_renderer.GetCamera();
            float focalLength = renderer::FocalLengthFromFovY(camera.FovY());
            if (ui::PropertyFloat("焦点距離", &focalLength, 12.0f, 200.0f,
                                  renderer::FocalLengthFromFovY(kDefaultCamera.fovY),
                                  "35mm フルサイズ換算。小さいほど広角で、遠近が強く出る",
                                  "%.0f mm", ImGuiSliderFlags_Logarithmic, 1.0f)) {
                camera.SetFovY(renderer::FovYFromFocalLength(focalLength));
            }
            ui::PropertyValue("画角", "%.1f 度（垂直）", RadiansToDegrees(camera.FovY()));
            ui::PropertyLabelEmpty("cameraReset");
            if (ui::Button("視点をリセット", ui::kWideButtonWidth)) {
                m_renderer.GetCamera().Reset();
            }
            ui::PropertyEnd();
            ui::EndPropertyTable();
        }
    }
    ImGui::End();
}

void Application::DrawLightingPanel() {
    if (ImGui::Begin("ライティング")) {
        renderer::LightSettings& light = m_renderer.Light();

        ui::SectionHeader("ライト");
        if (ui::BeginPropertyTable("lightRows")) {
            float azimuthDeg = RadiansToDegrees(light.azimuth);
            if (ui::PropertyFloat("方位角", &azimuthDeg, -180.0f, 180.0f,
                                  RadiansToDegrees(kDefaultLight.azimuth),
                                  "太陽の向き（水平方向）", "%.0f 度")) {
                light.azimuth = DegreesToRadians(azimuthDeg);
            }
            float elevationDeg = RadiansToDegrees(light.elevation);
            if (ui::PropertyFloat("仰角", &elevationDeg, -89.0f, 89.0f,
                                  RadiansToDegrees(kDefaultLight.elevation),
                                  "太陽の高さ。低いほど影が伸びる", "%.0f 度")) {
                light.elevation = DegreesToRadians(elevationDeg);
            }
            ui::PropertyFloat("照度", &light.illuminance, 0.0f, 200000.0f,
                              kDefaultLight.illuminance,
                              "lux。晴天の直射日光がおよそ 100000 lux", "%.0f");
            ui::PropertyColorLinear("光の色", &light.color.x, &kDefaultLight.color.x);
            ui::PropertyBool("影", &m_renderer.ShadowEnabled(),
                             renderer::kPreviewDefaults.shadowEnabled,
                             "ディレクショナルライトの影を落とす。"
                             "ディスプレイスメントで押し出した形にも落ちる");
            ui::EndPropertyTable();
        }

        ui::SectionHeader("露出");
        renderer::ExposureSettings& exposure = m_renderer.Exposure();
        if (ui::BeginPropertyTable("exposureRows")) {
            ui::PropertyBool("EV を直接指定", &exposure.useManualEv, kDefaultExposure.useManualEv,
                             "オフにすると絞り / シャッター / ISO から EV100 を求める");
            if (exposure.useManualEv) {
                ui::PropertyFloat("EV100", &exposure.manualEv100, -6.0f, 20.0f,
                                  kDefaultExposure.manualEv100, nullptr, "%.2f");
            } else {
                ui::PropertyFloat("絞り", &exposure.aperture, 1.0f, 32.0f,
                                  kDefaultExposure.aperture, "F 値。大きいほど暗くなる", "F%.1f");

                float shutterDenominator = 1.0f / exposure.shutterSpeed;
                if (ui::PropertyFloat("シャッター", &shutterDenominator, 1.0f, 4000.0f,
                                      1.0f / kDefaultExposure.shutterSpeed,
                                      "秒の逆数。大きいほど暗くなる", "1/%.0f 秒",
                                      ImGuiSliderFlags_Logarithmic)) {
                    exposure.shutterSpeed = 1.0f / shutterDenominator;
                }
                ui::PropertyFloat("ISO", &exposure.iso, 50.0f, 6400.0f, kDefaultExposure.iso,
                                  "感度。大きいほど明るくなる", "%.0f",
                                  ImGuiSliderFlags_Logarithmic);
            }
            ui::PropertyValue("EV100", "%.2f  (exposure %.3e)", exposure.Ev100(),
                              exposure.Exposure());
            ui::EndPropertyTable();
        }

        // **環境そのもの（何を空にするか）は天球パネルが持つ。**
        // ここに残すのは、天球ではなく見え方に属する設定だけ。
        ui::SectionHeader("環境 (IBL)");
        if (ui::BeginPropertyTable("iblRows")) {
            const renderer::SkyAsset* activeSky = m_skyLibrary.Active();
            ui::PropertyValue("天球", "%s", (activeSky != nullptr) ? activeSky->name.c_str() : "-");
            ui::PropertyValue("環境", "%s", m_renderer.GetEnvironment().SourceName().c_str());
            ui::PropertyValue("equirect", "%u x %u", m_renderer.GetEnvironment().EquirectWidth(),
                              m_renderer.GetEnvironment().EquirectHeight());
            ui::PropertyBool("背景を表示", &m_renderer.ShowSkybox(),
                             renderer::kPreviewDefaults.showSkybox,
                             "オフにすると背景色だけになる。IBL の寄与は残る");
            ImGui::BeginDisabled(!m_renderer.ShowSkybox());
            ui::PropertyBool("背景をぼかす", &m_renderer.SkyboxBlur(),
                             renderer::kPreviewDefaults.skyboxBlur,
                             "背景だけを柔らかくする。素材を見比べるときに、"
                             "背景の細部が目移りの原因にならないようにする。"
                             "IBL の寄与と陰影は変わらない");
            ImGui::EndDisabled();
            ui::EndPropertyTable();
        }
        ui::HintText("空の切り替えと輝度は「天球」パネルで設定する");

        ui::SectionHeader("トーンマップ");
        if (ui::BeginPropertyTable("tonemapRows")) {
            static const char* const kTonemapLabels[] = {"なし", "Reinhard", "ACES"};
            int tonemap = static_cast<int>(m_renderer.Tonemap());
            if (ui::PropertyCombo("方式", &tonemap, kTonemapLabels, IM_ARRAYSIZE(kTonemapLabels),
                                  static_cast<int>(renderer::kPreviewDefaults.tonemap))) {
                m_renderer.Tonemap() = static_cast<renderer::TonemapMode>(tonemap);
            }
            ui::EndPropertyTable();
        }
    }
    ImGui::End();
}

}  // namespace mm
