// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Lifecycle contract for top-level application modules (Dock, Top Bar,
// search, updates...). The Application starts modules in dependency order and
// stops them in reverse order. A module that fails to start is reported and
// skipped; it must not prevent unrelated modules from running.
#pragma once

#include "core/Result.h"

#include <string_view>

namespace aether {

class IModule {
public:
    virtual ~IModule() = default;

    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

    // Called on the UI thread. Must not block on network or disk-heavy work; start that work
    // asynchronously instead.
    virtual Result<void> Start() = 0;

    // Called on the UI thread during shutdown. Must be idempotent, must not throw, and must release
    // every resource (windows, hooks, threads, COM objects) before returning.
    virtual void Stop() noexcept = 0;
};

}  // namespace aether
