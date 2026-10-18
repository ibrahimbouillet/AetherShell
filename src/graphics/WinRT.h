// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Single place that pulls in the Direct2D/DirectWrite/WIC and
// Windows.UI.Composition (C++/WinRT + ABI interop) headers in the right order.
#pragma once

#include <windows.h>
#include <Unknwn.h>

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>

#include <DispatcherQueue.h>
#include <windows.ui.composition.interop.h>

namespace aether::graphics {
namespace wuc = winrt::Windows::UI::Composition;
namespace abi_wuc = ABI::Windows::UI::Composition;
}  // namespace aether::graphics
