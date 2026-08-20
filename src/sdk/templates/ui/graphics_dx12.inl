std::string Dx12OverlayResourcesHeaderModule() {
    return R"URK(#pragma once

#include <array>
#include <cstdint>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <backends/imgui_impl_dx12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace ModRenderHook {

enum class Dx12BeginFrameStatus {
    ready,
    gpu_busy,
    unavailable,
    reset_failed,
};

struct Dx12FrameSubmission {
    ID3D12GraphicsCommandList *commandList{};
    ID3D12Resource *backBuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
    UINT frameIndex{};
};

class Dx12OverlayResources final {
  public:
    using DiagnosticSink = void (*)(const char *message);

    Dx12OverlayResources() = default;
    ~Dx12OverlayResources();

    Dx12OverlayResources(const Dx12OverlayResources &) = delete;
    Dx12OverlayResources &operator=(const Dx12OverlayResources &) = delete;

    void set_diagnostic_sink(DiagnosticSink sink) noexcept;
    [[nodiscard]] bool capture_command_queue(ID3D12CommandQueue *queue) noexcept;
    [[nodiscard]] bool has_command_queue() const noexcept;
    [[nodiscard]] bool create(IDXGISwapChain *swapChain) noexcept;
    [[nodiscard]] bool wait_for_idle() noexcept;
    void release_device_objects() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] Dx12BeginFrameStatus begin_frame(Dx12FrameSubmission *submission) noexcept;
    [[nodiscard]] bool submit_frame(const Dx12FrameSubmission &submission) noexcept;
    [[nodiscard]] bool complete_frame(const Dx12FrameSubmission &submission) noexcept;

    [[nodiscard]] ID3D12Device *device() const noexcept;
    [[nodiscard]] ID3D12CommandQueue *command_queue() const noexcept;
    [[nodiscard]] ID3D12DescriptorHeap *srv_heap() const noexcept;
    [[nodiscard]] DXGI_FORMAT format() const noexcept;
    [[nodiscard]] int frame_count() const noexcept;

    static void allocate_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle);
    static void free_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

  private:
    struct FrameContext {
        ID3D12CommandAllocator *allocator{};
        ID3D12Resource *backBuffer{};
        UINT64 fenceValue{};
    };

    static constexpr UINT kSrvDescriptorCapacity = 256;
    static Dx12OverlayResources *descriptorOwner_;

    [[noreturn]] void descriptor_failure(const char *message) const;
    void report(const char *message) const noexcept;

    DiagnosticSink diagnosticSink_{};
    ID3D12Device *device_{};
    ID3D12CommandQueue *commandQueue_{};
    IDXGISwapChain3 *swapChain_{};
    ID3D12DescriptorHeap *rtvHeap_{};
    ID3D12DescriptorHeap *srvHeap_{};
    ID3D12GraphicsCommandList *commandList_{};
    ID3D12Fence *fence_{};
    HANDLE fenceEvent_{};
    UINT rtvDescriptorSize_{};
    UINT srvDescriptorSize_{};
    UINT64 nextFenceValue_{1};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    bool synchronizationLost_{};
    std::array<bool, kSrvDescriptorCapacity> srvDescriptors_{};
    std::vector<FrameContext> frames_;
};

} // namespace ModRenderHook
)URK";
}

std::string Dx12OverlayResourcesSourceModule() {
    return R"URK(#include "dx12_overlay_resources.h"

#include <algorithm>
#include <exception>

namespace ModRenderHook {

Dx12OverlayResources *Dx12OverlayResources::descriptorOwner_ = nullptr;

Dx12OverlayResources::~Dx12OverlayResources() {
    shutdown();
}

void Dx12OverlayResources::set_diagnostic_sink(DiagnosticSink sink) noexcept {
    diagnosticSink_ = sink;
}

void Dx12OverlayResources::report(const char *message) const noexcept {
    if (diagnosticSink_)
        diagnosticSink_(message);
}

[[noreturn]] void Dx12OverlayResources::descriptor_failure(const char *message) const {
    report(message);
    std::terminate();
}

bool Dx12OverlayResources::capture_command_queue(ID3D12CommandQueue *queue) noexcept {
    if (!queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || commandQueue_)
        return false;
    queue->AddRef();
    commandQueue_ = queue;
    return true;
}

bool Dx12OverlayResources::has_command_queue() const noexcept {
    return commandQueue_ != nullptr;
}

bool Dx12OverlayResources::create(IDXGISwapChain *swapChain) noexcept {
    if (!swapChain || !commandQueue_ || device_ || !frames_.empty())
        return false;

    if (FAILED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void **>(&device_))) || !device_ ||
        FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&swapChain_))) ||
        !swapChain_) {
        report("DX12 swap chain, device, or command queue unavailable; UI resources were not created.");
        release_device_objects();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC descriptor{};
    if (FAILED(swapChain->GetDesc(&descriptor)) || descriptor.BufferCount == 0 ||
        descriptor.BufferDesc.Format == DXGI_FORMAT_UNKNOWN) {
        report("DX12 swap chain description is incomplete; UI resources were not created.");
        release_device_objects();
        return false;
    }
    format_ = descriptor.BufferDesc.Format;
    synchronizationLost_ = false;
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptor{};
    rtvDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescriptor.NumDescriptors = descriptor.BufferCount;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDescriptor, __uuidof(ID3D12DescriptorHeap),
                                              reinterpret_cast<void **>(&rtvHeap_))) ||
        !rtvHeap_) {
        report("DX12 RTV descriptor heap creation failed.");
        release_device_objects();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDescriptor{};
    srvDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescriptor.NumDescriptors = kSrvDescriptorCapacity;
    srvDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvDescriptor, __uuidof(ID3D12DescriptorHeap),
                                              reinterpret_cast<void **>(&srvHeap_))) ||
        !srvHeap_) {
        report("DX12 shader-visible descriptor heap creation failed.");
        release_device_objects();
        return false;
    }

    frames_.resize(descriptor.BufferCount);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < descriptor.BufferCount; ++index) {
        FrameContext &frame = frames_[index];
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   __uuidof(ID3D12CommandAllocator),
                                                   reinterpret_cast<void **>(&frame.allocator))) ||
            !frame.allocator ||
            FAILED(swapChain_->GetBuffer(index, __uuidof(ID3D12Resource),
                                         reinterpret_cast<void **>(&frame.backBuffer))) ||
            !frame.backBuffer) {
            report("DX12 frame resource creation failed.");
            release_device_objects();
            return false;
        }
        device_->CreateRenderTargetView(frame.backBuffer, nullptr, rtv);
        rtv.ptr += rtvDescriptorSize_;
    }

    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_.front().allocator, nullptr,
                                          __uuidof(ID3D12GraphicsCommandList),
                                          reinterpret_cast<void **>(&commandList_))) ||
        !commandList_ || FAILED(commandList_->Close()) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                    reinterpret_cast<void **>(&fence_))) ||
        !fence_) {
        report("DX12 command-list or fence creation failed.");
        release_device_objects();
        return false;
    }

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        report("DX12 fence event creation failed.");
        release_device_objects();
        return false;
    }

    descriptorOwner_ = this;
    return true;
}

bool Dx12OverlayResources::wait_for_idle() noexcept {
    if (synchronizationLost_)
        return false;
    if (!fence_ || !fenceEvent_)
        return true;
    const UINT64 lastSubmitted = nextFenceValue_ > 1 ? nextFenceValue_ - 1 : 0;
    if (lastSubmitted == 0 || fence_->GetCompletedValue() >= lastSubmitted)
        return true;
    if (FAILED(fence_->SetEventOnCompletion(lastSubmitted, fenceEvent_))) {
        report("DX12 fence event registration failed while waiting for overlay resources.");
        return false;
    }
    if (WaitForSingleObject(fenceEvent_, INFINITE) != WAIT_OBJECT_0) {
        report("DX12 fence wait failed while draining overlay resources.");
        return false;
    }
    return true;
}

void Dx12OverlayResources::release_device_objects() noexcept {
    for (FrameContext &frame : frames_) {
        if (frame.backBuffer)
            frame.backBuffer->Release();
        if (frame.allocator)
            frame.allocator->Release();
    }
    frames_.clear();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    if (fence_) {
        fence_->Release();
        fence_ = nullptr;
    }
    if (commandList_) {
        commandList_->Release();
        commandList_ = nullptr;
    }
    if (srvHeap_) {
        srvHeap_->Release();
        srvHeap_ = nullptr;
    }
    if (rtvHeap_) {
        rtvHeap_->Release();
        rtvHeap_ = nullptr;
    }
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    rtvDescriptorSize_ = 0;
    srvDescriptorSize_ = 0;
    nextFenceValue_ = 1;
    format_ = DXGI_FORMAT_UNKNOWN;
    synchronizationLost_ = false;
    srvDescriptors_.fill(false);
}

void Dx12OverlayResources::shutdown() noexcept {
    if (device_ || !frames_.empty())
        (void)wait_for_idle();
    release_device_objects();
    if (commandQueue_) {
        commandQueue_->Release();
        commandQueue_ = nullptr;
    }
    if (descriptorOwner_ == this)
        descriptorOwner_ = nullptr;
}

Dx12BeginFrameStatus Dx12OverlayResources::begin_frame(Dx12FrameSubmission *submission) noexcept {
    if (submission)
        *submission = {};
    if (!submission || synchronizationLost_ || !swapChain_ || !commandList_ || !fence_ || frames_.empty())
        return Dx12BeginFrameStatus::unavailable;

    const UINT frameIndex = swapChain_->GetCurrentBackBufferIndex();
    if (frameIndex >= frames_.size())
        return Dx12BeginFrameStatus::unavailable;
    FrameContext &frame = frames_[frameIndex];
    if (frame.fenceValue != 0 && fence_->GetCompletedValue() < frame.fenceValue)
        return Dx12BeginFrameStatus::gpu_busy;
    if (FAILED(frame.allocator->Reset()) || FAILED(commandList_->Reset(frame.allocator, nullptr)))
        return Dx12BeginFrameStatus::reset_failed;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex) * rtvDescriptorSize_;
    *submission = {commandList_, frame.backBuffer, rtv, frameIndex};
    return Dx12BeginFrameStatus::ready;
}

bool Dx12OverlayResources::submit_frame(const Dx12FrameSubmission &submission) noexcept {
    if (!commandQueue_ || !submission.commandList || submission.frameIndex >= frames_.size() ||
        FAILED(submission.commandList->Close())) {
        return false;
    }
    ID3D12CommandList *commandLists[] = {submission.commandList};
    commandQueue_->ExecuteCommandLists(1, commandLists);
    return true;
}

bool Dx12OverlayResources::complete_frame(const Dx12FrameSubmission &submission) noexcept {
    if (!commandQueue_ || !fence_ || submission.frameIndex >= frames_.size())
        return false;
    const UINT64 fenceValue = nextFenceValue_++;
    if (FAILED(commandQueue_->Signal(fence_, fenceValue))) {
        synchronizationLost_ = true;
        report("DX12 overlay fence signal failed; frame resources will not be reused.");
        return false;
    }
    frames_[submission.frameIndex].fenceValue = fenceValue;
    return true;
}

ID3D12Device *Dx12OverlayResources::device() const noexcept {
    return device_;
}

ID3D12CommandQueue *Dx12OverlayResources::command_queue() const noexcept {
    return commandQueue_;
}

ID3D12DescriptorHeap *Dx12OverlayResources::srv_heap() const noexcept {
    return srvHeap_;
}

DXGI_FORMAT Dx12OverlayResources::format() const noexcept {
    return format_;
}

int Dx12OverlayResources::frame_count() const noexcept {
    return static_cast<int>(frames_.size());
}

void Dx12OverlayResources::allocate_srv_descriptor(ImGui_ImplDX12_InitInfo *,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                                                    D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle) {
    Dx12OverlayResources *owner = descriptorOwner_;
    if (!owner || !cpuHandle || !gpuHandle || !owner->srvHeap_ || !owner->srvDescriptorSize_)
        owner ? owner->descriptor_failure("DX12 ImGui SRV descriptor allocation received invalid state.")
              : std::terminate();

    for (UINT index = 0; index < kSrvDescriptorCapacity; ++index) {
        if (owner->srvDescriptors_[index])
            continue;
        owner->srvDescriptors_[index] = true;
        *cpuHandle = owner->srvHeap_->GetCPUDescriptorHandleForHeapStart();
        *gpuHandle = owner->srvHeap_->GetGPUDescriptorHandleForHeapStart();
        cpuHandle->ptr += static_cast<SIZE_T>(index) * owner->srvDescriptorSize_;
        gpuHandle->ptr += static_cast<UINT64>(index) * owner->srvDescriptorSize_;
        return;
    }
    owner->descriptor_failure("DX12 ImGui exhausted its 256-entry shader-visible SRV descriptor heap.");
}

void Dx12OverlayResources::free_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                D3D12_GPU_DESCRIPTOR_HANDLE) {
    Dx12OverlayResources *owner = descriptorOwner_;
    if (!owner || !owner->srvHeap_ || !owner->srvDescriptorSize_)
        owner ? owner->descriptor_failure("DX12 ImGui SRV descriptor release received invalid state.")
              : std::terminate();

    const SIZE_T first = owner->srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
    if (cpuHandle.ptr < first)
        owner->descriptor_failure("DX12 ImGui attempted to release an invalid SRV descriptor.");
    const SIZE_T offset = cpuHandle.ptr - first;
    if (offset % owner->srvDescriptorSize_ != 0)
        owner->descriptor_failure("DX12 ImGui attempted to release an unaligned SRV descriptor.");
    const SIZE_T index = offset / owner->srvDescriptorSize_;
    if (index >= kSrvDescriptorCapacity || !owner->srvDescriptors_[index])
        owner->descriptor_failure("DX12 ImGui attempted to release an unknown SRV descriptor.");
    owner->srvDescriptors_[index] = false;
}

} // namespace ModRenderHook
)URK";
}


