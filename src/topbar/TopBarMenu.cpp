// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "topbar/TopBarMenu.h"

namespace aether::topbar {

std::vector<ui::MenuEntry> BuildTopBarMenu(ItemKind item, TaskbarReplacementState taskbar, bool updateAvailable) {
    const auto command = [](BarCommand id, const wchar_t* label, std::optional<graphics::Glyph> glyph = std::nullopt) {
        auto entry = ui::MenuEntry::Command(static_cast<std::uint32_t>(id), label);
        entry.glyph = glyph;  // glass menus draw it in their icon column; HMENU presenters ignore it
        return entry;
    };
    std::vector<ui::MenuEntry> entries;
    const auto notification = [&] {
        if (!taskbar.available) return;
        if (taskbar.suspended) entries.push_back(command(BarCommand::ResumeReplacement, L"&Resume taskbar replacement", graphics::Glyph::Taskbar));
        else if (taskbar.active) entries.push_back(command(BarCommand::RevealWindowsTaskbar,
            L"Show Windows &notification area for 60 seconds", graphics::Glyph::Taskbar));
    };
    switch (item) {
        case ItemKind::Logo:
            entries = {command(BarCommand::Search, L"&Search…", graphics::Glyph::Search),
                       command(BarCommand::Settings, L"&Settings…", graphics::Glyph::Settings)};
            notification();
            if (updateAvailable) entries.push_back(command(BarCommand::ViewUpdate, L"View available &update…", graphics::Glyph::Download));
            entries.push_back(command(BarCommand::CheckUpdates, L"&Check for updates…", graphics::Glyph::Updates));
            entries.push_back(ui::MenuEntry::Separator());
            entries.push_back(command(BarCommand::About, L"&About Aether Shell", graphics::Glyph::About));
            entries.push_back(command(BarCommand::Quit, L"&Quit Aether Shell", graphics::Glyph::Power));
            break;
        case ItemKind::Keyboard: entries.push_back(command(BarCommand::Keyboard, L"Switch &input language\tWin+Space")); break;
        case ItemKind::Network: entries.push_back(command(BarCommand::Network, L"Open &network controls…")); break;
        case ItemKind::Weather: entries.push_back(command(BarCommand::Weather, L"&Weather settings…")); break;
        case ItemKind::Volume:
            entries = {command(BarCommand::ToggleVolume, L"Toggle output &mute"), command(BarCommand::SoundSettings, L"&Sound settings…")};
            break;
        case ItemKind::Microphone:
            entries = {command(BarCommand::ToggleMicrophone, L"Toggle microphone &mute"),
                command(BarCommand::VoiceTyping, L"Start Windows &voice typing\tWin+H"),
                command(BarCommand::MicrophoneSettings, L"Microphone &privacy settings…")};
            break;
        case ItemKind::Battery: entries.push_back(command(BarCommand::PowerSettings, L"&Power settings…")); break;
        case ItemKind::Clock: entries.push_back(command(BarCommand::Clock, L"&Calendar and notifications…")); break;
        case ItemKind::NotificationArea: notification(); break;
        case ItemKind::QuickControls: break; // transient panel is opened by the host, not a menu
    }
    return entries;
}

Result<void> RestoreVoiceTypingTarget(ui::IMenuWindowOps& ops, HWND owner, HWND previousForeground) {
    if (!owner || !previousForeground || previousForeground == owner || !ops.IsValidWindow(previousForeground))
        return MakeError(ErrorCode::Unavailable, "Windows voice typing needs a previous application window");
    const HWND foreground = ops.Foreground();
    if (foreground == previousForeground) return Ok();
    if (foreground != owner)
        return MakeError(ErrorCode::Unavailable, "Foreground changed; choose the text field and try voice typing again");
    if (!ops.SetForeground(previousForeground) || ops.Foreground() != previousForeground)
        return MakeError(ErrorCode::AccessDenied, "The previous application could not regain focus for voice typing");
    return Ok();
}

}  // namespace aether::topbar
