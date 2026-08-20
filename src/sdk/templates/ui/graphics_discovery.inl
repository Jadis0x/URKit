std::string DxgiHookDiscoveryHeaderModule() {
    return R"URK(#pragma once

namespace ModRenderHook {

struct DxgiVTableTargets {
    void *present{};
    void *present1{};
    void *resizeBuffers{};
};

using DxgiDiscoveryDiagnosticSink = void (*)(const char *message);

[[nodiscard]] DxgiVTableTargets discover_dxgi_hook_targets(bool preferDx12,
                                                            DxgiDiscoveryDiagnosticSink diagnosticSink);
[[nodiscard]] void *discover_dx12_execute_command_lists_target(DxgiDiscoveryDiagnosticSink diagnosticSink);

} // namespace ModRenderHook
)URK";
}

std::string DxgiHookDiscoverySourceModule() {
    return R"URK(#include "dxgi_hook_discovery.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iterator>

namespace ModRenderHook {
namespace {

void report(DxgiDiscoveryDiagnosticSink sink, const char *message) {
    if (sink)
        sink(message);
}

[[nodiscard]] bool readable_range(const void *pointer, std::size_t bytes) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!pointer || bytes == 0 || VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= base && bytes <= (base + memory.RegionSize) - address;
}

[[nodiscard]] bool executable(const void *pointer) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!pointer || VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const DWORD protection = memory.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool capture_targets(IDXGISwapChain *swapChain, const char *probeName,
                                   DxgiVTableTargets *targets, DxgiDiscoveryDiagnosticSink sink) {
    if (!swapChain || !targets || !readable_range(swapChain, sizeof(void *))) {
        report(sink, "DXGI probe did not return a readable swap chain.");
        return false;
    }
    void **vtable = *reinterpret_cast<void ***>(swapChain);
    if (!readable_range(vtable, sizeof(void *) * 23)) {
        report(sink, "DXGI probe swap-chain vtable is unreadable; UI not installed.");
        return false;
    }
    targets->present = executable(vtable[8]) ? vtable[8] : nullptr;
    targets->present1 = executable(vtable[22]) ? vtable[22] : nullptr;
    targets->resizeBuffers = executable(vtable[13]) ? vtable[13] : nullptr;
    if ((targets->present || targets->present1) && targets->resizeBuffers)
        return true;

    char text[160]{};
    std::snprintf(text, sizeof(text), "%s probe found non-executable swap-chain hook targets; UI not installed.",
                  probeName ? probeName : "DXGI");
    report(sink, text);
    return false;
}

class ProbeWindow final {
  public:
    explicit ProbeWindow(bool preferDx12) {
        std::snprintf(className_, sizeof(className_), "URK_%s_Probe_%lu_%lu_%lu", preferDx12 ? "DX12" : "DX11",
                      GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
        windowClass_ = {sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr),
                        nullptr, nullptr, nullptr, nullptr, className_, nullptr};
        registered_ = RegisterClassExA(&windowClass_) != 0;
        if (registered_) {
            window_ = CreateWindowA(className_, className_, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                                    windowClass_.hInstance, nullptr);
        }
    }

    ~ProbeWindow() {
        if (window_)
            DestroyWindow(window_);
        if (registered_)
            UnregisterClassA(className_, windowClass_.hInstance);
    }

    [[nodiscard]] HWND get() const noexcept { return window_; }
    [[nodiscard]] bool registered() const noexcept { return registered_; }

  private:
    char className_[96]{};
    WNDCLASSEXA windowClass_{};
    HWND window_{};
    bool registered_{};
};

} // namespace

DxgiVTableTargets discover_dxgi_hook_targets(bool preferDx12, DxgiDiscoveryDiagnosticSink sink) {
    DxgiVTableTargets targets{};
    ProbeWindow probe(preferDx12);
    if (!probe.registered()) {
        report(sink, "DXGI probe window class registration failed; UI not installed.");
        return targets;
    }
    if (!probe.get()) {
        report(sink, "DXGI probe window creation failed; UI not installed.");
        return targets;
    }

    if (preferDx12) {
        ID3D12Device *device = nullptr;
        ID3D12CommandQueue *queue = nullptr;
        IDXGIFactory4 *factory = nullptr;
        IDXGISwapChain1 *swapChain1 = nullptr;
        IDXGISwapChain *swapChain = nullptr;
        HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                           reinterpret_cast<void **>(&device));
        if (SUCCEEDED(result) && device) {
            D3D12_COMMAND_QUEUE_DESC queueDescriptor{};
            queueDescriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            result = device->CreateCommandQueue(&queueDescriptor, __uuidof(ID3D12CommandQueue),
                                                reinterpret_cast<void **>(&queue));
        }
        if (SUCCEEDED(result) && queue)
            result = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory));
        if (SUCCEEDED(result) && factory) {
            DXGI_SWAP_CHAIN_DESC1 descriptor{};
            descriptor.Width = 100;
            descriptor.Height = 100;
            descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.SampleDesc.Count = 1;
            descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            descriptor.BufferCount = 2;
            descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            result = factory->CreateSwapChainForHwnd(queue, probe.get(), &descriptor, nullptr, nullptr, &swapChain1);
        }
        if (SUCCEEDED(result) && swapChain1)
            result = swapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void **>(&swapChain));
        if (SUCCEEDED(result) && swapChain) {
            (void)capture_targets(swapChain, "DX12", &targets, sink);
        } else {
            char text[160]{};
            std::snprintf(text, sizeof(text), "DX12 probe swap-chain creation failed (hr=0x%08X); UI not installed.",
                          static_cast<unsigned>(result));
            report(sink, text);
        }
        if (swapChain)
            swapChain->Release();
        if (swapChain1)
            swapChain1->Release();
        if (factory)
            factory->Release();
        if (queue)
            queue->Release();
        if (device)
            device->Release();
        return targets;
    }

    DXGI_SWAP_CHAIN_DESC descriptor{};
    descriptor.BufferCount = 1;
    descriptor.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    descriptor.OutputWindow = probe.get();
    descriptor.SampleDesc.Count = 1;
    descriptor.Windowed = TRUE;
    descriptor.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    constexpr D3D_DRIVER_TYPE driverTypes[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};

    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    IDXGISwapChain *swapChain = nullptr;
    D3D_FEATURE_LEVEL createdLevel{};
    HRESULT result = E_FAIL;
    for (D3D_DRIVER_TYPE driverType : driverTypes) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr, 0, featureLevels,
                                              static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                                              &descriptor, &swapChain, &device, &createdLevel, &context);
        if (SUCCEEDED(result) && swapChain)
            break;
        if (swapChain) {
            swapChain->Release();
            swapChain = nullptr;
        }
        if (context) {
            context->Release();
            context = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
    }
    if (SUCCEEDED(result) && swapChain) {
        (void)capture_targets(swapChain, "DX11", &targets, sink);
    } else {
        char text[160]{};
        std::snprintf(text, sizeof(text),
                      "DXGI probe device creation failed (hr=0x%08X); DX11/DX12 overlay unavailable.",
                      static_cast<unsigned>(result));
        report(sink, text);
    }
    if (swapChain)
        swapChain->Release();
    if (context)
        context->Release();
    if (device)
        device->Release();
    return targets;
}

void *discover_dx12_execute_command_lists_target(DxgiDiscoveryDiagnosticSink sink) {
    ID3D12Device *device = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                 reinterpret_cast<void **>(&device))) ||
        !device) {
        report(sink, "DX12 probe device creation failed; DX12 overlay will remain unavailable.");
        return nullptr;
    }
    D3D12_COMMAND_QUEUE_DESC descriptor{};
    descriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr;
    const HRESULT result =
        device->CreateCommandQueue(&descriptor, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    void *target = nullptr;
    if (SUCCEEDED(result) && queue && readable_range(queue, sizeof(void *))) {
        void **vtable = *reinterpret_cast<void ***>(queue);
        if (readable_range(vtable, sizeof(void *) * 11) && executable(vtable[10])) {
            target = vtable[10];
        } else {
            report(sink, "DX12 ExecuteCommandLists target is unreadable or non-executable.");
        }
    } else {
        report(sink, "DX12 probe command queue creation failed; DX12 overlay will remain unavailable.");
    }
    if (queue)
        queue->Release();
    device->Release();
    return target;
}

} // namespace ModRenderHook
)URK";
}


