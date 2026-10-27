// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "topbar/TopBarModel.h"
#include "ui/PopupMenu.h"

namespace aether::topbar {

enum class BarCommand : std::uint32_t {
    Search = 1, Settings, CheckUpdates, About, Quit, ViewUpdate,
    Keyboard, Network, Weather, ToggleVolume, SoundSettings, ToggleMicrophone,
    MicrophoneSettings, PowerSettings, Clock, RevealWindowsTaskbar, ResumeReplacement, VoiceTyping,
    Calendar, HiddenIcons
};

// Pure menu model; no shell action, input injection, audio mutation or native popup is performed.
[[nodiscard]] std::vector<ui::MenuEntry> BuildTopBarMenu(ItemKind item, TaskbarReplacementState taskbar,
                                                       bool updateAvailable);

// User-triggered UI-thread focus handoff before Win+H is requested. Never targets the Top Bar,
// overrides a third window that took foreground, or succeeds without verifying the restored target.
// Tests inject window operations; no keyboard input is sent here.
[[nodiscard]] Result<void> RestoreVoiceTypingTarget(ui::IMenuWindowOps& ops, HWND owner, HWND previousForeground);

}  // namespace aether::topbar
