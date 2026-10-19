// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/InputInjection.h"

#include "diagnostics/Logger.h"

#include <algorithm>
#include <limits>

namespace aether::platform {
namespace {

class SendInputInjector final : public IKeyboardInjector {
public:
    UINT Send(std::span<INPUT> inputs) noexcept override {
        if (inputs.empty() || inputs.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return 0;
        return ::SendInput(static_cast<UINT>(inputs.size()), inputs.data(), static_cast<int>(sizeof(INPUT)));
    }
};

INPUT KeyEvent(WORD virtualKey, bool up) noexcept {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | (IsExtendedKey(virtualKey) ? KEYEVENTF_EXTENDEDKEY : 0u);
    return input;
}

}  // namespace

Result<std::unique_ptr<IKeyboardInjector>> MakeSendInputInjector() try {
    return std::unique_ptr<IKeyboardInjector>(std::make_unique<SendInputInjector>());
} catch (...) { return MakeError(ErrorCode::Internal, "keyboard injector unavailable"); }

IKeyboardInjector& SystemKeyboardInjector() noexcept {
    static SendInputInjector injector;
    return injector;
}

bool IsExtendedKey(WORD virtualKey) noexcept {
    switch (virtualKey) {
        case VK_LWIN: case VK_RWIN: case VK_APPS:
        case VK_RCONTROL: case VK_RMENU:
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_DIVIDE: case VK_NUMLOCK: case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}

Result<std::vector<INPUT>> BuildChordInputs(std::span<const WORD> keys) {
    try {
        if (keys.empty() || keys.size() > kMaxChordKeys) {
            return MakeError(ErrorCode::InvalidArgument, "a keyboard shortcut needs 1 to 8 keys");
        }
        for (std::size_t index = 0; index < keys.size(); ++index) {
            if (keys[index] == 0 || keys[index] > 0xFE) return MakeError(ErrorCode::InvalidArgument, "invalid virtual key");
            if (std::find(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(index), keys[index]) !=
                keys.begin() + static_cast<std::ptrdiff_t>(index)) {
                return MakeError(ErrorCode::InvalidArgument, "a keyboard shortcut repeats a key");
            }
        }
        std::vector<INPUT> inputs;
        inputs.reserve(keys.size() * 2);
        for (const WORD key : keys) inputs.push_back(KeyEvent(key, false));
        for (auto key = keys.rbegin(); key != keys.rend(); ++key) inputs.push_back(KeyEvent(*key, true));
        return inputs;
    } catch (...) { return MakeError(ErrorCode::Internal, "keyboard shortcut could not be prepared"); }
}

Result<void> SendKeyChord(IKeyboardInjector& injector, std::span<const WORD> keys) {
    try {
        auto inputs = BuildChordInputs(keys);
        if (!inputs) return inputs.error();
        const UINT sent = injector.Send(*inputs);
        if (sent >= inputs->size()) return Ok();
        // Events are inserted in order: downs first, then ups in reverse. Keys [0, pressed) went down
        // and the last `released` of them came back up; the rest must be released now.
        const std::size_t count = keys.size();
        const std::size_t pressed = std::min<std::size_t>(sent, count);
        const std::size_t released = sent > count ? sent - count : 0;
        std::vector<INPUT> recovery;
        for (std::size_t index = pressed - released; index > 0; --index) recovery.push_back(KeyEvent(keys[index - 1], true));
        if (!recovery.empty()) {
            const UINT recovered = injector.Send(recovery);
            for (std::size_t index = recovered; index < recovery.size(); ++index) {
                (void)injector.Send(std::span<INPUT>(&recovery[index], 1));
            }
        }
        AETHER_LOG_WARN("input", "Windows accepted {} of {} keyboard events; released keys that went down", sent,
                        inputs->size());
        return MakeError(ErrorCode::Platform, sent == 0 ? "Windows did not accept the keyboard shortcut"
                                                        : "Windows accepted only part of the keyboard shortcut");
    } catch (...) { return MakeError(ErrorCode::Internal, "keyboard shortcut could not be sent"); }
}

}  // namespace aether::platform
