// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/SystemPalette.h"

#include "diagnostics/Logger.h"
#include "graphics/WinRT.h"

#include <winrt/Windows.UI.ViewManagement.h>

namespace aether::graphics {
namespace {

namespace wuvm = winrt::Windows::UI::ViewManagement;

Color FromWinRT(const winrt::Windows::UI::Color& color) noexcept {
    return Color{static_cast<float>(color.R) / 255.0f, static_cast<float>(color.G) / 255.0f,
                 static_cast<float>(color.B) / 255.0f, 1.0f};
}

}  // namespace

SystemPalette QuerySystemPalette() noexcept {
    SystemPalette palette;
    try {
        const wuvm::UISettings settings;
        const auto read = [&](wuvm::UIColorType type) -> std::optional<Color> {
            try {
                return FromWinRT(settings.GetColorValue(type));
            } catch (...) {
                return std::nullopt;
            }
        };
        palette.accent = read(wuvm::UIColorType::Accent);
        palette.accentLight1 = read(wuvm::UIColorType::AccentLight1);
        palette.accentLight2 = read(wuvm::UIColorType::AccentLight2);
        palette.accentLight3 = read(wuvm::UIColorType::AccentLight3);
        palette.accentDark1 = read(wuvm::UIColorType::AccentDark1);
        palette.accentDark2 = read(wuvm::UIColorType::AccentDark2);
        palette.accentDark3 = read(wuvm::UIColorType::AccentDark3);
        try {
            palette.textScale = static_cast<float>(settings.TextScaleFactor());
        } catch (...) {
            palette.textScale = 1.0f;
        }
        palette.available = palette.accent.has_value();
    } catch (const winrt::hresult_error& error) {
        AETHER_LOG_WARN("graphics", "Windows accent palette unavailable (0x{:08X}); using the brand palette",
                        static_cast<unsigned>(error.code().value));
        palette = SystemPalette{};
    } catch (...) {
        palette = SystemPalette{};
    }
    return palette;
}

DesignTokens MakeShellDesignTokens(const AppearanceOptions& options) {
    return MakeDesignTokens(DetectShellThemeMode(options.theme), QuerySystemPalette(), options);
}

DesignTokens MakeAppDesignTokens(const AppearanceOptions& options) {
    return MakeDesignTokens(DetectAppThemeMode(options.theme), QuerySystemPalette(), options);
}

}  // namespace aether::graphics
