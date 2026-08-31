#pragma once

#include "rhi/Device.h"

#include <DirectXMath.h>

#include <vector>

namespace mm::renderer {

// PipelineCache の VertexLayout::MeshStandard と対応する頂点。
struct MeshVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 tangent;  // w は従法線の向き
    DirectX::XMFLOAT2 uv;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

// GPU 上のメッシュ。頂点・インデックスとも DEFAULT ヒープに置く。
class Mesh {
public:
    bool Create(rhi::Device& device, const MeshData& data, const wchar_t* debugName);
    void Release(rhi::Device& device);

    void Draw(ID3D12GraphicsCommandList* commandList) const;

    bool IsValid() const { return m_indexCount > 0; }
    uint32_t IndexCount() const { return m_indexCount; }
    uint32_t VertexCount() const { return m_vertexCount; }

private:
    rhi::GpuBuffer m_vertexBuffer;
    rhi::GpuBuffer m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;
};

// プリミティブ生成。マテリアルの見え方を確かめるための最小限。
MeshData MakeSphere(uint32_t segments, uint32_t rings, float radius);
MeshData MakePlane(float size, uint32_t subdivisions);
MeshData MakeCube(float size);

}  // namespace mm::renderer
