#pragma once

#include <DirectXMath.h>

#include <cstdint>

namespace hm::renderer {

// 注視点を中心に回る軌道カメラ。マテリアルプレビューではこれで十分。
class Camera {
public:
    void Orbit(float deltaYaw, float deltaPitch);
    void Pan(float deltaX, float deltaY);
    void Zoom(float delta);
    void Reset();

    void SetViewportSize(uint32_t width, uint32_t height);

    DirectX::XMMATRIX ViewMatrix() const;
    DirectX::XMMATRIX ProjectionMatrix() const;
    DirectX::XMFLOAT3 Position() const;

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
