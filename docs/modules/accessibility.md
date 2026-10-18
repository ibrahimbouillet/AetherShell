<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# accessibility — UI Automation for custom surfaces

`src/ui/accessibility`, target `aether_accessibility`, implements a reusable flat UI Automation
fragment for a custom-drawn HWND. It supports NFR-007 and the Dock, Top Bar and Search requirements
FR-004, FR-011 and FR-013. It does not add a desktop surface or change real system settings.

Dependencies are the existing core/diagnostics/platform libraries and Windows system libraries
`uiautomationcore`, `ole32`, `oleaut32`, `windowsapp`. There is no third-party dependency.

## API and integration

Include `ui/accessibility/AccessibleSurface.h`, namespace `aether::ui::accessibility`.

`IAccessibleSurfaceModel` supplies a root name/control type, a flat sequence of `AccessibleItem`
values, and `Result<void>` actions `Invoke(id)`, `Toggle(id)`, `SetFocus(id)`. An item has a positive
stable ID, name/help/status, control type, physical-screen-pixel rectangle, enabled/focus/offscreen
flags, optional invoke/toggle patterns, and toggle state. IDs must remain attached to logical items
when visual order changes. The module rejects duplicates, nonpositive IDs and invalid rectangles.
The model owns actual keyboard navigation, focus state, rendering and service actions.

```cpp
// UI thread, already initialized as COM STA, after the HWND exists:
accessibility::AccessibleSurface accessibility;
auto connected = accessibility.Connect(hwnd, model);

// Forward WM_GETOBJECT from SurfaceWindow::OnMessage:
LRESULT answer = 0;
if (accessibility.HandleGetObject(hwnd, wParam, lParam, answer)) return answer;

// After relevant model changes:
auto structure = accessibility.NotifyStructureChanged();
auto property = accessibility.NotifyPropertyChanged(itemId); // name and item status
auto focus = accessibility.NotifyFocusChanged(itemId);
auto invoked = accessibility.NotifyInvoked(itemId);

// WM_DESTROY / SurfaceWindow::OnDestroying, before the model dies:
accessibility.Disconnect();
```

The surface also exposes an owning `RootProvider()` reference for native integration. Applications
must handle failed `Result` values; the sketch omits application-specific error presentation.

## COM ownership and threading

Connect, all notifications, WM_GETOBJECT handling and Disconnect run on the HWND's creating COM STA
thread. Connect rejects a missing window, a window owned by another thread and an uninitialized/MTA
apartment. The UI thread must pump Windows/COM messages while an automation client is calling it.

Providers advertise `ServerSideProvider | UseComThreading` and use standard COM apartment marshaling;
they deliberately expose no agile object or free-threaded marshaler. Microsoft documents that
[UseComThreading sends STA provider calls back to their own STA](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcore/ne-uiautomationcore-provideroptions).
Each provider additionally rejects a direct wrong-thread call before reading the model. Model
exceptions become HRESULT failures and their potentially private messages are never logged.

Root and child COM objects have atomic reference counts and a shared connection state. The surface
owns a root and the known children until disconnection. Disconnect first closes model access, then
calls `UiaDisconnectProvider` for every provider and `UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)`,
and releases the owning references. External COM references can outlive the window/model; property,
navigation and action calls then return `UIA_E_ELEMENTNOTAVAILABLE`. The caller must disconnect on
the owner thread before model destruction. Destroying the surface/model from another thread is
outside this API's ownership contract.

## Exposed UIA behavior

- Root: `IRawElementProviderSimple`, `IRawElementProviderFragment`,
  `IRawElementProviderFragmentRoot`, native HWND host provider, root name and control type.
- Items: simple/fragment interfaces; Invoke and Toggle are advertised only for supported patterns.
  Disabled actions fail with `UIA_E_ELEMENTNOTENABLED`; unsupported actions fail explicitly.
- Name, help text, status, automation ID, control type, enabled, focusable/focused, offscreen and
  content/control-element properties are exposed. Automation IDs are `item-<stableId>`.
- Navigation supports parent, first/last child and bounded next/previous sibling. Root runtime ID
  delegates to the HWND host; each item appends `[UiaAppendRuntimeId, stableId]` to that identity.
- Bounding rectangles come from current model physical screen coordinates, not DIPs. Root bounds
  come from the real HWND. Hit testing uses half-open rectangles and ignores offscreen items.
- GetFocus returns the item whose model says it has keyboard focus. Embedded fragment roots are
  empty. Item removal disconnects its old COM reference after a structure notification.
- Structure, focus, name/status property changes and invocation have explicit notification methods.
  Clients may read current model state between notifications; changes to the child set must be
  followed by `NotifyStructureChanged()` before clients navigate the new children.

## Verification and limits

Tests use only synthetic models and their own test HWNDs; no desktop capture, external UI operation,
application launch, registry/audio/work-area mutation or global keyboard action is performed.

`tests/unit/accessibility/NavigationTests.cpp` checks navigation boundaries/empty fragments and pure
stable runtime IDs. `tests/integration/accessibility/AccessibleSurfaceTests.cpp` creates a real
top-level test window, a model of three items and a separate MTA `CUIAutomation` client while the UI
STA pumps messages. It checks names/control types/bounds, Invoke/Toggle on the UI thread, a received
focus event, identity after reorder, unavailable removed/destroyed elements, malformed models,
wrong apartments, supported patterns and disabled actions.

The integration focus handler compares incoming identities only with its own test element; it does
not read other applications' names or content. Actual Narrator navigation, high-contrast rendering,
the finished Dock/Top Bar/Search keyboard flow, Windows 10 and monitor/DPI changes require later
product-level qualification. Text/Edit control types do not imply a Text/Value pattern: editable
input should use the native Win32 EDIT control as required by the Search work order.

Observed 2026-10-18: Debug MSVC build in previously nonexistent `build/wf-accessibility2`, `/W4 /WX`,
no warnings, 3 unit cases and 3 integration cases passing (2/2 CTest executables). Command:

```powershell
./scripts/build.ps1 -BuildDir build/wf-accessibility2 -Modules accessibility -Jobs 2 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex accessibility
```

Initial compiler failures exposed required COM-header ordering and independent Fragment/FragmentRoot
interfaces, then a test handler's C++/WinRT final modifier and a signed HRESULT assertion. Those were
corrected before the passing runs. No other modules, shared contracts, root documentation or Git
history were changed by this module implementation.
