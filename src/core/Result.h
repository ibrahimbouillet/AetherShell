// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Explicit error handling for module boundaries. Public module APIs return
// Result<T> instead of throwing; exceptions must never cross a module boundary
// or a thread boundary.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace aether {

enum class ErrorCode : std::uint16_t {
    Unknown = 1,
    InvalidArgument,
    InvalidState,
    NotFound,
    AlreadyExists,
    Unsupported,
    Unavailable,
    AccessDenied,
    Timeout,
    Cancelled,
    Network,
    Http,
    RateLimited,
    Parse,
    Validation,
    Integrity,
    Signature,
    InsufficientSpace,
    Io,
    Platform,
    Internal,
};

[[nodiscard]] std::string_view ToString(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    // UTF-8, human readable. Must never contain credentials, tokens or other secrets.
    std::string message;
    // HRESULT, Win32 error code or HTTP status when relevant; 0 otherwise.
    std::int64_t platformCode = 0;

    // "Timeout: request exceeded 15000 ms (0x80072EE2)"
    [[nodiscard]] std::string Describe() const;
};

[[nodiscard]] inline Error MakeError(ErrorCode code, std::string message, std::int64_t platformCode = 0) {
    return Error{code, std::move(message), platformCode};
}

namespace detail {
[[noreturn]] void ResultAccessFailure(const Error* error) noexcept;
}

template <class T>
class [[nodiscard]] Result {
    static_assert(!std::is_same_v<std::remove_cvref_t<T>, Error>, "Result<Error> is ambiguous");
    static_assert(!std::is_reference_v<T>, "Result<T&> is not supported");

public:
    Result(const T& value) : storage_(std::in_place_index<0>, value) {}
    Result(T&& value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    template <class... Args>
    explicit Result(std::in_place_t, Args&&... args) : storage_(std::in_place_index<0>, std::forward<Args>(args)...) {}

    [[nodiscard]] bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & {
        if (!ok()) detail::ResultAccessFailure(&std::get<1>(storage_));
        return std::get<0>(storage_);
    }
    [[nodiscard]] const T& value() const& {
        if (!ok()) detail::ResultAccessFailure(&std::get<1>(storage_));
        return std::get<0>(storage_);
    }
    [[nodiscard]] T&& value() && {
        if (!ok()) detail::ResultAccessFailure(&std::get<1>(storage_));
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] const Error& error() const {
        if (ok()) detail::ResultAccessFailure(nullptr);
        return std::get<1>(storage_);
    }

    template <class U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return ok() ? std::get<0>(storage_) : static_cast<T>(std::forward<U>(fallback));
    }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

private:
    std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error& error() const {
        if (!error_) detail::ResultAccessFailure(nullptr);
        return *error_;
    }

private:
    std::optional<Error> error_;
};

[[nodiscard]] inline Result<void> Ok() { return {}; }

}  // namespace aether
