#include "rhi/PipelineCache.h"

#include "core/Log.h"
#include "rhi/ShaderCompiler.h"

namespace hm::rhi {
namespace {

D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(UINT shaderRegister, D3D12_FILTER filter,
                                            D3D12_TEXTURE_ADDRESS_MODE addressMode,
                                            UINT maxAnisotropy = 1) {
    D3D12_STATIC_SAMPLER_DESC desc = {};
    desc.Filter = filter;
    desc.AddressU = addressMode;
    desc.AddressV = addressMode;
    desc.AddressW = addressMode;
    desc.MaxAnisotropy = maxAnisotropy;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = D3D12_FLOAT32_MAX;
    desc.ShaderRegister = shaderRegister;
    desc.RegisterSpace = 0;
    desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    return desc;
}

}  // namespace

PipelineCache::~PipelineCache() {
    Destroy();
}

bool PipelineCache::Create(ID3D12Device* device, ShaderCompiler* compiler) {
    m_device = device;
    m_compiler = compiler;
    return CreateGlobalRootSignature();
}

void PipelineCache::Destroy() {
    m_computePipelines.clear();
    m_rootSignature.Reset();
    m_device = nullptr;
    m_compiler = nullptr;
}

bool PipelineCache::CreateGlobalRootSignature() {
    D3D12_ROOT_PARAMETER1 params[2] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = kRootConstantCount;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    const D3D12_STATIC_SAMPLER_DESC samplers[] = {
        MakeStaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        MakeStaticSampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        MakeStaticSampler(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
        MakeStaticSampler(3, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 16),
    };

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = _countof(params);
    desc.Desc_1_1.pParameters = params;
    desc.Desc_1_1.NumStaticSamplers = _countof(samplers);
    desc.Desc_1_1.pStaticSamplers = samplers;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                          D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;
    const HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            HM_LOG_ERROR("ルートシグネチャのシリアライズに失敗: %s",
                         static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    if (!HM_CHECK_HR(m_device->CreateRootSignature(0, blob->GetBufferPointer(),
                                                   blob->GetBufferSize(),
                                                   IID_PPV_ARGS(&m_rootSignature)))) {
        return false;
    }
    m_rootSignature->SetName(L"GlobalRootSignature");
    return true;
}

ID3D12PipelineState* PipelineCache::GetCompute(const std::wstring& relativePath,
                                               const std::wstring& entryPoint) {
    const std::wstring key = relativePath + L"#" + entryPoint;
    if (const auto it = m_computePipelines.find(key); it != m_computePipelines.end()) {
        return it->second.Get();
    }

    if (m_compiler == nullptr || !m_rootSignature) {
        return nullptr;
    }

    ComPtr<IDxcBlob> bytecode = m_compiler->Compile(relativePath, entryPoint.c_str(), L"cs_6_6");
    if (!bytecode) {
        return nullptr;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_rootSignature.Get();
    desc.CS.pShaderBytecode = bytecode->GetBufferPointer();
    desc.CS.BytecodeLength = bytecode->GetBufferSize();

    ComPtr<ID3D12PipelineState> pipeline;
    if (!HM_CHECK_HR(m_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)))) {
        return nullptr;
    }
    pipeline->SetName(key.c_str());

    const auto [inserted, ok] = m_computePipelines.emplace(key, std::move(pipeline));
    (void)ok;
    return inserted->second.Get();
}

void PipelineCache::InvalidateAll() {
    m_computePipelines.clear();
}

}  // namespace hm::rhi
