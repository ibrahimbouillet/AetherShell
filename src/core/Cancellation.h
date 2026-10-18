// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Cooperative cancellation for long-running work (network requests, indexing,
// downloads). A default-constructed token is never cancelled.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace aether {

namespace detail {
struct CancellationState;
}

class CancellationToken {
public:
    CancellationToken() = default;

    [[nodiscard]] bool IsCancelled() const noexcept;
    [[nodiscard]] bool CanBeCancelled() const noexcept { return state_ != nullptr; }

    // RAII registration. Destroying it unregisters the callback and, if the callback is running on
    // another thread at that moment, waits for it to finish. That guarantee lets a callback safely
    // touch resources (for example a WinHTTP handle) owned by the code holding the registration.
    class Registration {
    public:
        Registration() = default;
        Registration(Registration&& other) noexcept;
        Registration& operator=(Registration&& other) noexcept;
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        ~Registration();
        void Reset() noexcept;

    private:
        friend class CancellationToken;
        Registration(std::shared_ptr<detail::CancellationState> state, std::uint64_t id) noexcept;
        std::shared_ptr<detail::CancellationState> state_;
        std::uint64_t id_ = 0;
    };

    // Invokes `callback` once when cancellation is requested. If the token is already cancelled the
    // callback runs synchronously before this function returns.
    [[nodiscard]] Registration OnCancel(std::function<void()> callback) const;

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<detail::CancellationState> state) noexcept : state_(std::move(state)) {}
    std::shared_ptr<detail::CancellationState> state_;
};

class CancellationSource {
public:
    CancellationSource();
    [[nodiscard]] CancellationToken Token() const noexcept;
    void Cancel() noexcept;
    [[nodiscard]] bool IsCancelled() const noexcept;

private:
    std::shared_ptr<detail::CancellationState> state_;
};

}  // namespace aether
