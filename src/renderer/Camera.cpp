#include "renderer/Camera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace hm::renderer {
namespace {

// 真上・真下でビュー行列が縮退しないよう、わずかに手前で止める。
constexpr float kPitchLimit = 1.55334306f;  // 89 度

}  // namespace

void Camera::Orbit(float deltaX, float deltaY) {
    // 右手系では画面の右が +X なので、ヨーは符号を反転する。
    // そうしないと、ドラッグした向きと逆に内容が回る。
    m_yaw -= deltaX;
    m_pitch = std::clamp(m_pitch + deltaY, -kPitchLimit, kPitchLimit);
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

CameraBasis Camera::Basis() const {
    const XMFLOAT3 position = Position();
    const XMVECTOR eye = XMLoadFloat3(&position);
    const XMVECTOR target = XMLoadFloat3(&m_target);
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // 右手系なので 右 = 前 × 上。ビュー行列が作る基底と一致する。
    const XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, eye));
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(forward, worldUp));
    const XMVECTOR up = XMVector3Cross(right, forward);

    CameraBasis basis;
    XMStoreFloat3(&basis.right, right);
    XMStoreFloat3(&basis.up, up);
    XMStoreFloat3(&basis.forward, forward);
    return basis;
}

XMMATRIX Camera::ViewMatrix() const {
    const XMFLOAT3 position = Position();
    // 右手系。LH を使うと画面が左右反転し、+X が画面左に出てしまう。
    return XMMatrixLookAtRH(XMLoadFloat3(&position), XMLoadFloat3(&m_target),
                            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

XMMATRIX Camera::ProjectionMatrix() const {
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    // ビュー行列と手系を揃える。深度の範囲は LH 版と同じ [0, 1]。
    return XMMatrixPerspectiveFovRH(m_fovY, aspect, m_nearZ, m_farZ);
}

}  // namespace hm::renderer
