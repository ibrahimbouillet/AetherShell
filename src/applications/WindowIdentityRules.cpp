// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/WindowIdentityRules.h"

#include "applications/ApplicationIdentity.h"
#include "core/Strings.h"

namespace aether::apps {
namespace {

bool UsableAumid(std::wstring_view aumid) noexcept { return IsPackagedAumid(aumid) || IsExplicitAumid(aumid); }

// A frame whose CoreWindow belongs to another process than the frame itself.
bool IsAttachedFrame(const WindowIdentityFacts& facts) noexcept {
    return facts.hostedProcessId != 0 && facts.hostedProcessId != facts.windowProcessId;
}

}  // namespace

bool IsApplicationFrameClass(const std::wstring& className) noexcept {
    return className == kApplicationFrameWindowClass;
}

WindowIdentity ResolveWindowIdentity(const WindowIdentityFacts& facts) {
    const bool frame = IsApplicationFrameClass(facts.className);
    const bool attached = frame && IsAttachedFrame(facts);
    const auto windowAumid = text::Trim(facts.windowAumid);
    const bool windowAumidUsable = UsableAumid(windowAumid);

    // Detached frame (app minimized or suspended): keep what was seen while it was attached, unless
    // the frame now carries a different application's AUMID.
    if (frame && !attached && facts.rememberedFrameIdentity) {
        const WindowIdentity& remembered = *facts.rememberedFrameIdentity;
        if (!windowAumidUsable || text::EqualsIgnoreCase(windowAumid, remembered.appUserModelId)) return remembered;
    }

    WindowIdentity identity;
    identity.processId = attached ? facts.hostedProcessId : facts.windowProcessId;
    if (windowAumidUsable) {
        identity.appUserModelId = std::wstring(windowAumid);
    } else if (!facts.processAumid.empty()) {
        identity.appUserModelId = facts.processAumid;
    }
    if (facts.processOpened) identity.executablePath = facts.processExecutable;

    if (!identity.appUserModelId.empty()) {
        if (auto id = MakeIdForAumid(identity.appUserModelId)) {
            identity.id = std::move(id).value();
        } else {
            identity.appUserModelId.clear();
        }
    }
    if (identity.id.empty() && !identity.executablePath.empty()) {
        PathPolicy policy;
        policy.allowUnc = true;  // identity only: nothing is launched from this path, and no short-name
                                 // expansion is attempted on UNC paths (see MakeIdForExecutable)
        if (auto id = MakeIdForExecutable(identity.executablePath, policy)) identity.id = std::move(id).value();
    }
    if (identity.id.empty()) {
        // Process not accessible (or its image path unusable): still list the window, grouped by class.
        identity.appUserModelId.clear();
        identity.executablePath.clear();
        if (auto id = MakeIdForWindowClass(facts.className)) identity.id = std::move(id).value();
    }
    return identity;
}

bool ShouldRememberFrameIdentity(const WindowIdentityFacts& facts, const WindowIdentity& identity) {
    if (!IsApplicationFrameClass(facts.className) || identity.appUserModelId.empty()) return false;
    return IsAttachedFrame(facts) || !facts.rememberedFrameIdentity.has_value();
}

}  // namespace aether::apps
