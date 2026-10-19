// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Additive presentation fields of the shared application contract.
#include "AetherTest.h"

#include "applications/ApplicationTypes.h"

using namespace aether::apps;

AETHER_TEST("application types: presentation fields default to empty and keep positional initialization") {
    const ApplicationInfo info;
    CHECK(info.iconResource.empty());
    const RunningApplication running;
    CHECK(running.relaunchIconResource.empty());
    CHECK(running.relaunchDisplayName.empty());
    // Existing aggregate initializations that name only leading members still compile and leave the
    // new trailing members empty.
    const ApplicationInfo partial{L"exe:synthetic"};
    CHECK_EQ(partial.id, std::wstring(L"exe:synthetic"));
    CHECK(partial.iconResource.empty());
    RunningApplication copy;
    copy.relaunchIconResource = L"synthetic.dll,-1";
    copy.relaunchDisplayName = L"Synthetic profile";
    const RunningApplication copied = copy;
    CHECK_EQ(copied.relaunchIconResource, copy.relaunchIconResource);
    CHECK_EQ(copied.relaunchDisplayName, copy.relaunchDisplayName);
}
