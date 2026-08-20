std::string Dx11ViewportSwapChainHeaderModule() {
    return R"URK(#pragma once

#include <optional>

#include <dxgi.h>

namespace ModRenderHook {

struct Dx11ViewportSwapChainConfig {
    DXGI_SWAP_CHAIN_DESC descriptor{};
    bool flipModel = false;
};

[[nodiscard]] std::optional<Dx11ViewportSwapChainConfig> make_dx11_viewport_swap_chain_config(
    const DXGI_SWAP_CHAIN_DESC &gameDescriptor);

[[nodiscard]] const char *dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect);

} // namespace ModRenderHook
)URK";
}

std::string Dx11ViewportSwapChainSourceModule() {
    return R"URK(#include "dx11_viewport_swap_chain.h"

#include <algorithm>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool is_known_swap_effect(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool is_flip_model(DXGI_SWAP_EFFECT effect) {
    return effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

} // namespace

std::optional<Dx11ViewportSwapChainConfig> make_dx11_viewport_swap_chain_config(
    const DXGI_SWAP_CHAIN_DESC &gameDescriptor) {
    if (!is_known_swap_effect(gameDescriptor.SwapEffect) || gameDescriptor.BufferDesc.Format == DXGI_FORMAT_UNKNOWN) {
        return std::nullopt;
    }

    Dx11ViewportSwapChainConfig config{};
    config.flipModel = is_flip_model(gameDescriptor.SwapEffect);

    DXGI_SWAP_CHAIN_DESC &viewport = config.descriptor;
    viewport.BufferDesc.Width = 0;
    viewport.BufferDesc.Height = 0;
    viewport.BufferDesc.RefreshRate = {0, 1};
    viewport.BufferDesc.Format = gameDescriptor.BufferDesc.Format;
    viewport.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    viewport.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    viewport.SampleDesc = {1, 0};
    viewport.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    viewport.BufferCount = config.flipModel ? (std::clamp)(gameDescriptor.BufferCount, 2u, 16u)
                                            : (std::clamp)(gameDescriptor.BufferCount, 1u, 16u);
    viewport.OutputWindow = nullptr;
    viewport.Windowed = TRUE;
    viewport.SwapEffect = gameDescriptor.SwapEffect;
    viewport.Flags = 0;
    return config;
}

const char *dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:
            return "DISCARD";
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
            return "SEQUENTIAL";
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
            return "FLIP_SEQUENTIAL";
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            return "FLIP_DISCARD";
        default:
            return "UNKNOWN";
    }
}

} // namespace ModRenderHook
)URK";
}

std::string Dx11StateGuardHeaderModule() {
    return R"URK(#pragma once
#include <array>
#include <d3d11.h>

namespace ModRenderHook {
class Dx11OutputMergerStateGuard final {
  public:
    explicit Dx11OutputMergerStateGuard(ID3D11DeviceContext *context) noexcept;
    ~Dx11OutputMergerStateGuard();

    Dx11OutputMergerStateGuard(const Dx11OutputMergerStateGuard &) = delete;
    Dx11OutputMergerStateGuard &operator=(const Dx11OutputMergerStateGuard &) = delete;

  private:
    ID3D11DeviceContext *context_{};
    std::array<ID3D11RenderTargetView *, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets_{};
    ID3D11DepthStencilView *depth_stencil_{};
};
} // namespace ModRenderHook
)URK";
}

std::string Dx11StateGuardSourceModule() {
    return R"URK(#include "dx11_state_guard.h"

namespace ModRenderHook {
Dx11OutputMergerStateGuard::Dx11OutputMergerStateGuard(ID3D11DeviceContext *context) noexcept : context_(context) {
    if (context_)
        context_->OMGetRenderTargets(static_cast<UINT>(render_targets_.size()), render_targets_.data(),
                                     &depth_stencil_);
}

Dx11OutputMergerStateGuard::~Dx11OutputMergerStateGuard() {
    if (!context_)
        return;

    context_->OMSetRenderTargets(static_cast<UINT>(render_targets_.size()), render_targets_.data(), depth_stencil_);
    for (ID3D11RenderTargetView *render_target : render_targets_)
        if (render_target)
            render_target->Release();
    if (depth_stencil_)
        depth_stencil_->Release();
}
} // namespace ModRenderHook
)URK";
}


