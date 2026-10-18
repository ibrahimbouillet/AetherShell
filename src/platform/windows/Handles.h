// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// RAII wrappers for Win32 handle types. COM interfaces use winrt::com_ptr.
#pragma once

#include <windows.h>

#include <utility>

namespace aether::platform {

template <class Traits>
class UniqueResource {
public:
    using Type = typename Traits::Type;

    UniqueResource() noexcept : value_(Traits::Invalid()) {}
    explicit UniqueResource(Type value) noexcept : value_(value) {}
    UniqueResource(UniqueResource&& other) noexcept : value_(other.Release()) {}
    UniqueResource& operator=(UniqueResource&& other) noexcept {
        if (this != &other) Reset(other.Release());
        return *this;
    }
    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;
    ~UniqueResource() { Reset(); }

    [[nodiscard]] Type Get() const noexcept { return value_; }
    [[nodiscard]] bool IsValid() const noexcept { return value_ != Traits::Invalid(); }
    explicit operator bool() const noexcept { return IsValid(); }

    [[nodiscard]] Type Release() noexcept { return std::exchange(value_, Traits::Invalid()); }
    void Reset(Type value = Traits::Invalid()) noexcept {
        if (value_ != Traits::Invalid()) Traits::Close(value_);
        value_ = value;
    }
    // For APIs that write a handle through an out-parameter.
    [[nodiscard]] Type* Put() noexcept {
        Reset();
        return &value_;
    }

private:
    Type value_;
};

struct HandleTraits {
    using Type = HANDLE;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type h) noexcept { ::CloseHandle(h); }
};

struct FileHandleTraits {
    using Type = HANDLE;
    static Type Invalid() noexcept { return INVALID_HANDLE_VALUE; }
    static void Close(Type h) noexcept { ::CloseHandle(h); }
};

struct ModuleTraits {
    using Type = HMODULE;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type h) noexcept { ::FreeLibrary(h); }
};

struct RegKeyTraits {
    using Type = HKEY;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type h) noexcept { ::RegCloseKey(h); }
};

struct IconTraits {
    using Type = HICON;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type h) noexcept { ::DestroyIcon(h); }
};

struct GdiObjectTraits {
    using Type = HGDIOBJ;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type h) noexcept { ::DeleteObject(h); }
};

struct RegionTraits {
    using Type = HRGN;
    static Type Invalid() noexcept { return nullptr; }
    static void Close(Type h) noexcept { ::DeleteObject(h); }
};

using UniqueHandle = UniqueResource<HandleTraits>;
using UniqueFileHandle = UniqueResource<FileHandleTraits>;
using UniqueModule = UniqueResource<ModuleTraits>;
using UniqueRegKey = UniqueResource<RegKeyTraits>;
using UniqueIcon = UniqueResource<IconTraits>;
using UniqueGdiObject = UniqueResource<GdiObjectTraits>;
using UniqueRegion = UniqueResource<RegionTraits>;

}  // namespace aether::platform
