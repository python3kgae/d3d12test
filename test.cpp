// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _WIN32
#include <wsl/winadapter.h>
#endif

#include <directx/d3d12.h>
#include <directx/dxcore.h>
#include <directx/d3dx12.h>
#include <dxguids/dxguids.h>
#include <wrl/client.h>

#include <fstream>
#include <iostream>
#include <iomanip>

#ifndef _WIN32
#include <unistd.h>
#include <dlfcn.h>
#endif


#define VERIFY_SUCCEEDED(expr) hr = (expr); if (FAILED(hr)) { std::cout << "FAILED: " #expr << ": " << std::hex << hr << std::endl; return hr; } else { std::cout << "Succeeded: " #expr << std::endl; }
#define VERIFY_ARE_EQUAL(a, b) if (a != b) { std::cout << "NOT EQUAL: " << a << ", " << b << std::endl; return 1; } else { std::cout << "Equal: " #a " and " #b << std::endl; }
using Microsoft::WRL::ComPtr;
int main()
{
    using FnDXCoreCreateAdapterFactory = HRESULT (*)(REFIID, void**);
#ifdef _WIN32
    auto dxcore = LoadLibraryA("dxcore.dll");
    auto d3d12 = LoadLibraryA("d3d12.dll");
    auto pfnDXCoreCreateAdapterFactory = (FnDXCoreCreateAdapterFactory)GetProcAddress(dxcore, "DXCoreCreateAdapterFactory");
    auto pfnD3D12CreateDevice = (decltype(&D3D12CreateDevice))GetProcAddress(d3d12, "D3D12CreateDevice");

#else
    auto dxcore = dlopen("libdxcore.so", RTLD_NOW | RTLD_LOCAL);
    auto d3d12 = dlopen("libd3d12.so", RTLD_NOW | RTLD_LOCAL);
    auto pfnDXCoreCreateAdapterFactory = (FnDXCoreCreateAdapterFactory)dlsym(dxcore, "DXCoreCreateAdapterFactory");
    auto pfnD3D12CreateDevice = (decltype(&D3D12CreateDevice))dlsym(d3d12, "D3D12CreateDevice");
#endif
    
    HRESULT hr = S_OK;
    
    ComPtr<IDXCoreAdapterFactory> spFactory;
    VERIFY_SUCCEEDED(pfnDXCoreCreateAdapterFactory(IID_PPV_ARGS(&spFactory)));
    ComPtr<IDXCoreAdapterList> spList;
    VERIFY_SUCCEEDED(spFactory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE, IID_PPV_ARGS(&spList)));
    ComPtr<IDXCoreAdapter> spAdapter;
    VERIFY_SUCCEEDED(spList->GetAdapter(0, IID_PPV_ARGS(&spAdapter)));

    ComPtr<ID3D12Device> spDevice;
    VERIFY_SUCCEEDED(pfnD3D12CreateDevice(spAdapter.Get(), D3D_FEATURE_LEVEL_1_0_CORE, IID_PPV_ARGS(&spDevice)));

    ComPtr<ID3D12Resource> spUAV, spStaging;
    auto UAVDesc = CD3DX12_RESOURCE_DESC::Buffer(65536, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto UAVHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto StagingDesc = CD3DX12_RESOURCE_DESC::Buffer(65536);
    auto StagingHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

    VERIFY_SUCCEEDED(spDevice->CreateCommittedResource(&UAVHeapDesc, D3D12_HEAP_FLAG_SHARED, &UAVDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&spUAV)));
    VERIFY_SUCCEEDED(spDevice->CreateCommittedResource(&StagingHeapDesc, D3D12_HEAP_FLAG_NONE, &StagingDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&spStaging)));

    HANDLE hRawSharedHandle;
    VERIFY_SUCCEEDED(spDevice->CreateSharedHandle(spUAV.Get(), nullptr, 0x10000000L, nullptr, &hRawSharedHandle));

    ComPtr<ID3D12Resource> spUAV2, spUAV3;
    VERIFY_SUCCEEDED(spDevice->OpenSharedHandle(hRawSharedHandle, IID_PPV_ARGS(&spUAV2)));
    VERIFY_SUCCEEDED(spDevice->OpenSharedHandle(hRawSharedHandle, IID_PPV_ARGS(&spUAV3)));
#ifdef _WIN32
    CloseHandle(hRawSharedHandle);
#else
    close((int)(intptr_t)hRawSharedHandle);
#endif

    std::ifstream shaderFile("shader.cso",  std::ios::binary);
    // Load pre-compiled shaders.
    VERIFY_ARE_EQUAL(shaderFile.good(), true);

    std::vector<char> shaderData((std::istreambuf_iterator<char>(shaderFile)), std::istreambuf_iterator<char>());

    shaderFile.close();

    ComPtr<ID3D12RootSignature> spRootSig;
    VERIFY_SUCCEEDED(spDevice->CreateRootSignature(0, shaderData.data(), shaderData.size(), IID_PPV_ARGS(&spRootSig)));

    ComPtr<ID3D12PipelineState> spPSO;
    D3D12_COMPUTE_PIPELINE_STATE_DESC PSODesc = {};
    PSODesc.CS.pShaderBytecode = shaderData.data();
    PSODesc.CS.BytecodeLength = shaderData.size();
    VERIFY_SUCCEEDED(spDevice->CreateComputePipelineState(&PSODesc, IID_PPV_ARGS(&spPSO)));

    ComPtr<ID3D12CommandAllocator> spAllocator;
    VERIFY_SUCCEEDED(spDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&spAllocator)));

    ComPtr<ID3D12GraphicsCommandList> spCommandList;
    VERIFY_SUCCEEDED(spDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, spAllocator.Get(), spPSO.Get(), IID_PPV_ARGS(&spCommandList)));
    spCommandList->SetComputeRootSignature(spRootSig.Get());
    spCommandList->SetComputeRootUnorderedAccessView(0, spUAV->GetGPUVirtualAddress());
    spCommandList->SetPipelineState(spPSO.Get());
    spCommandList->Dispatch(1, 1, 1);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(spUAV.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    spCommandList->ResourceBarrier(1, &barrier);

    spCommandList->CopyResource(spStaging.Get(), spUAV2.Get());
    VERIFY_SUCCEEDED(spCommandList->Close());

    ComPtr<ID3D12CommandQueue> spQueue;
    D3D12_COMMAND_QUEUE_DESC CQDesc = { D3D12_COMMAND_LIST_TYPE_COMPUTE };
    VERIFY_SUCCEEDED(spDevice->CreateCommandQueue(&CQDesc, IID_PPV_ARGS(&spQueue)));
    spQueue->ExecuteCommandLists(1, CommandListCast(spCommandList.GetAddressOf()));

    ComPtr<ID3D12Fence> spFence;
    VERIFY_SUCCEEDED(spDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&spFence)));
    VERIFY_SUCCEEDED(spQueue->Signal(spFence.Get(), 1));

    VERIFY_SUCCEEDED(spFence->SetEventOnCompletion(1, nullptr));
    
    VERIFY_ARE_EQUAL(spFence->GetCompletedValue(), 1ull);

    void* pData = nullptr;
    VERIFY_SUCCEEDED(spStaging->Map(0, nullptr, &pData));

    VERIFY_ARE_EQUAL(*reinterpret_cast<UINT*>(pData), 0xd3d12u);
}


