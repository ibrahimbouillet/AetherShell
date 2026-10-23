// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "showcase/PresentationPlan.h"
#include <Windows.h>
#include <memory>

namespace aether::showcase {
// Explicit-run-only actions. All lookup roots are verified Aether HWNDs in one live process;
// no arbitrary selector, application launch, network toggle or settings edit is supported.
class NativePresentationActions final : public IPresentationActions {
public:
    NativePresentationActions() noexcept;
    ~NativePresentationActions() override;
    NativePresentationActions(NativePresentationActions&&) noexcept;
    NativePresentationActions& operator=(NativePresentationActions&&) noexcept;
    NativePresentationActions(const NativePresentationActions&) = delete;
    NativePresentationActions& operator=(const NativePresentationActions&) = delete;
    [[nodiscard]] static Result<NativePresentationActions> Create(HWND dock, HWND topbar,
        std::wstring verifiedExplorerPinName = {}) noexcept;
    [[nodiscard]] Result<void> Prepare(bool changesVolume) override;
    [[nodiscard]] Result<void> Apply(const PresentationAction& action) override;
    [[nodiscard]] Result<void> Restore() noexcept override;
    [[nodiscard]] Result<void> ValidateVisibleControls() noexcept;
private:
    struct Impl;
    explicit NativePresentationActions(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
}
