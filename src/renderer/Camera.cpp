#include "renderer/Camera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace hm::renderer {
namespace {

// 真上・真下でビュー行列が縮退しないよう、わずかに手前で止める。
constexpr float kPitchLimit = 1.55334306f;  // 89 度

}  // namespace

void Camera::Orbit(float deltaYaw, float deltaPitch) {
    m_yaw += deltaYaw;
    m_pitch = std::clamp(m_pitch + deltaPitch, -kPitchLimit, kPitchLimit);
}

void Camera::Pan(float deltaX, float deltaY) {
    const XMVECTOR forward = XMVectorSet(std::cos(m_pitch) * std::sin(m_yaw), std::sin(m_pitch),
                                         std::cos(m_pitch) * std::cos(m_yaw), 0.0f);
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    const XMVECTOR up = XMVector3Cross(forward, right);

    // 距離に比例させ、遠くにいるほど大きく動くようにする。
    const float scale = m_distance * 0.0015f;
    XMVECTOR target = XMLoadFloat3(&m_target);
    target = XMVectorAdd(target, XMVectorScale(right, -deltaX * scale));
    target = XMVectorAdd(target, XMVectorScale(up, deltaY * scale));
    XMStoreFloat3(&m_target, target);
}

void Camera::Zoom(float delta) {
    m_distance = std::clamp(m_distance * std::pow(1.1f, -delta), 0.1f, 100.0f);
}

void Camera::Reset() {
    m_target = {0.0f, 0.0f, 0.0f};
    m_distance = 3.2f;
    m_yaw = 0.6f;
    m_pitch = 0.35f;
}

void Camera::SetViewportSize(uint32_t width, uint32_t height) {
    m_width = (width > 0) ? width : 1;
    m_height = (height > 0) ? height : 1;
}

XMFLOAT3 Camera::Position() const {
    const float x = m_target.x + m_distance * std::cos(m_pitch) * std::sin(m_yaw);
    const float y = m_target.y + m_distance * std::sin(m_pitch);
    const float z = m_target.z + m_distance * std::cos(m_pitch) * std::cos(m_yaw);
    return XMFLOAT3{x, y, z};
}

XMMATRIX Camera::ViewMatrix() const {
    const XMFLOAT3 position = Position();
    return XMMatrixLookAtLH(XMLoadFloat3(&position), XMLoadFloat3(&m_target),
                            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

XMMATRIX Camera::ProjectionMatrix() const {
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    return XMMatrixPerspectiveFovLH(m_fovY, aspect, m_nearZ, m_farZ);
}

}  // namespace hm::renderer
