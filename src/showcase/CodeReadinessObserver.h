// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "showcase/CleanSession.h"

#include <Windows.h>

#include <memory>

namespace aether::showcase {

// Start after closing pre-existing Code windows and before the showcase's real Code launch.
// The worker observes only UI Automation workbench markers in a visible exact-Code.exe window;
// it does not open apps, capture pixels, inspect documents, or log application data. Readiness
// means loaded English workbench controls, not completion of document/network/extension work.
class CodeReadinessObserver final {
public:
    CodeReadinessObserver() noexcept;
    ~CodeReadinessObserver();
    CodeReadinessObserver(const CodeReadinessObserver&) = delete;
    CodeReadinessObserver& operator=(const CodeReadinessObserver&) = delete;
    CodeReadinessObserver(CodeReadinessObserver&& other) noexcept;
    CodeReadinessObserver& operator=(CodeReadinessObserver&& other) noexcept;

    [[nodiscard]] static Result<CodeReadinessObserver> Start() noexcept;
    // No UI Automation/provider calls occur in these capture-thread accessors.
    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool Failed() const noexcept;
    [[nodiscard]] HWND ReadyWindow() const noexcept;
    [[nodiscard]] ProcessIdentity ReadyProcess() const noexcept;

private:
    struct Impl;
    explicit CodeReadinessObserver(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
