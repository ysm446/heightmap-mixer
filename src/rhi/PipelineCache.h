#pragma once

#include "rhi/Common.h"

#include <string>
#include <unordered_map>

namespace hm::rhi {

class ShaderCompiler;

// 全パス共通のルートシグネチャ。bindless 前提で、
// シェーダはリソースをディスクリプタヒープのインデックスで直接引く。
//
//   b0 : ルート定数 16 dword（テクスチャのインデックスや小さなパラメータ）
//   b1 : ルート CBV（大きめの定数バッファ）
//   s0-s3 : スタティックサンプラ
//
// これにより、パスごとにルートシグネチャを作る必要がなくなる。
inline constexpr uint32_t kRootConstantCount = 16;

// PSO とルートシグネチャのキャッシュ。
// シェーダのホットリロード時は InvalidateAll() で作り直す。
class PipelineCache {
public:
    PipelineCache() = default;
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    bool Create(ID3D12Device* device, ShaderCompiler* compiler);
    void Destroy();

    ID3D12RootSignature* GlobalRootSignature() const { return m_rootSignature.Get(); }

    // コンピュート PSO を取得する。未生成ならコンパイルして作る。失敗時は nullptr。
    ID3D12PipelineState* GetCompute(const std::wstring& relativePath,
                                    const std::wstring& entryPoint);

    // キャッシュを破棄する。GPU がまだ参照している可能性があるため、
    // 呼び出し側は事前に GPU 待機するか、削除キューへ渡すこと。
    void InvalidateAll();

    size_t PipelineCount() const { return m_computePipelines.size(); }

private:
    bool CreateGlobalRootSignature();

    ID3D12Device* m_device = nullptr;
    ShaderCompiler* m_compiler = nullptr;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    std::unordered_map<std::wstring, ComPtr<ID3D12PipelineState>> m_computePipelines;
};

}  // namespace hm::rhi
