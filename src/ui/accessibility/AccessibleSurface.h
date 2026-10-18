// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <winrt/base.h>

#include <cstddef>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace aether::ui::accessibility {

struct AccessibleItem {
    int id = 0;  // Positive stable identity; never use a visual index as an identity.
    std::wstring name;
    std::wstring helpText;
    std::wstring itemStatus;
    CONTROLTYPEID controlType = UIA_ButtonControlTypeId;
    RECT boundsPx{};  // Physical screen coordinates, including the monitor's DPI conversion.
    bool isEnabled = true;
    bool isFocusable = true;
    bool hasKeyboardFocus = false;
    bool isOffscreen = false;
    bool supportsInvoke = false;
    bool supportsToggle = false;
    ToggleState toggleState = ToggleState_Off;
};

// Every method is called on the HWND's COM STA thread. Keep model actions short: dispatch any
// launch/discovery/disk work to the appropriate service. Model and surface are UI-thread owned.
class IAccessibleSurfaceModel {
public:
    virtual ~IAccessibleSurfaceModel() = default;
    [[nodiscard]] virtual std::wstring RootName() const = 0;
    [[nodiscard]] virtual CONTROLTYPEID RootControlType() const = 0;
    [[nodiscard]] virtual std::size_t ItemCount() const = 0;
    [[nodiscard]] virtual AccessibleItem Item(std::size_t index) const = 0;
    virtual Result<void> Invoke(int id) = 0;
    virtual Result<void> Toggle(int id) = 0;
    virtual Result<void> SetFocus(int id) = 0;
};

// One flat UIA fragment per HWND. Connect, notifications, WM_GETOBJECT and Disconnect must run on
// the HWND's STA thread. Call Disconnect from WM_DESTROY, before destroying the model. External COM
// references may outlive both objects and then return UIA_E_ELEMENTNOTAVAILABLE safely.
class AccessibleSurface {
public:
    AccessibleSurface();
    ~AccessibleSurface();
    AccessibleSurface(const AccessibleSurface&) = delete;
    AccessibleSurface& operator=(const AccessibleSurface&) = delete;

    [[nodiscard]] Result<void> Connect(HWND hwnd, IAccessibleSurfaceModel& model);
    [[nodiscard]] bool HandleGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam, LRESULT& result) noexcept;
    void Disconnect() noexcept;
    [[nodiscard]] Result<void> NotifyStructureChanged();
    [[nodiscard]] Result<void> NotifyFocusChanged(int id);
    [[nodiscard]] Result<void> NotifyPropertyChanged(int id);
    [[nodiscard]] Result<void> NotifyInvoked(int id);

    // An owning reference, useful for native integrations and own-window tests. Called on the STA.
    [[nodiscard]] Result<winrt::com_ptr<IRawElementProviderSimple>> RootProvider() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Pure navigation helper: root uses index -1; nullopt means that direction has no neighbour.
[[nodiscard]] std::optional<int> NavigateIndex(int index, int count, NavigateDirection direction) noexcept;
[[nodiscard]] Result<std::array<int, 2>> RuntimeIdForItem(int id);

}  // namespace aether::ui::accessibility
