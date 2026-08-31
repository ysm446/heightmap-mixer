#pragma once

#include <DirectXMath.h>

#include <cstdint>

namespace hm::renderer {

// カメラのビュー空間の基底をワールド座標で表したもの。
// 座標軸ギズモのように「向きだけ」が要る用途に使う。
struct CameraBasis {
    DirectX::XMFLOAT3 right;    // 画面の右
    DirectX::XMFLOAT3 up;       // 画面の上
    DirectX::XMFLOAT3 forward;  // 画面の奥
};

// カメラの状態。プロジェクトの保存と読み込みで丸ごと出し入れする。
struct CameraState {
    DirectX::XMFLOAT3 target = {0.0f, 0.0f, 0.0f};
    float distance = 3.2f;
    float yaw = 0.6f;
    float pitch = 0.35f;
    float fovY = 0.7853981634f;
};

// 注視点を中心に回る軌道カメラ。マテリアルプレビューではこれで十分。
//
// 座標系は**右手系 Y-up**。X が右、Y が上、Z が手前（画面から見て奥が -Z）。
// 詳細は docs/design/rendering.md の「座標系」を参照。
class Camera {
public:
    // 画面上のドラッグ量で視点を回す。ドラッグした向きに内容が付いてくる。
    void Orbit(float deltaX, float deltaY);
    void Pan(float deltaX, float deltaY);
    void Zoom(float delta);
    void Reset();

    void SetViewportSize(uint32_t width, uint32_t height);

    DirectX::XMMATRIX ViewMatrix() const;
    DirectX::XMMATRIX ProjectionMatrix() const;
    DirectX::XMFLOAT3 Position() const;
    CameraBasis Basis() const;

    CameraState State() const;
    void SetState(const CameraState& state);

    float& FovY() { return m_fovY; }
    float& Distance() { return m_distance; }
    const DirectX::XMFLOAT3& Target() const { return m_target; }

private:
    DirectX::XMFLOAT3 m_target = {0.0f, 0.0f, 0.0f};
    float m_distance = 3.2f;
    float m_yaw = 0.6f;
    float m_pitch = 0.35f;
    float m_fovY = 0.7853981634f;  // 45 度
    float m_nearZ = 0.02f;
    float m_farZ = 200.0f;
    uint32_t m_width = 1;
    uint32_t m_height = 1;
};

}  // namespace hm::renderer
