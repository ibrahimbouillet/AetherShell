// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/ExplorerPinIdentity.h"
using namespace aether::showcase;
AETHER_TEST("showcase Explorer pin: trusted executable preserves the real localized name") {
    const auto result=ResolveExplorerPinName(R"({"dock":{"pinned":[{"id":"shell-pin","displayName":"Explorateur de fichiers","executablePath":"S:\\System\\explorer.exe"}]}})",L"S:\\System\\explorer.exe");
    REQUIRE(result);CHECK_EQ(*result,L"Explorateur de fichiers");
    const auto aumid=ResolveExplorerPinName(R"({"dock":{"pinned":[{"id":"shell-pin","displayName":"File Explorer","appUserModelId":"Microsoft.Windows.Explorer"}]}})",L"S:\\System\\explorer.exe");
    REQUIRE(aumid);CHECK_EQ(*aumid,L"File Explorer");
}
AETHER_TEST("showcase Explorer pin: spoofed executable ambiguous identity and sensitive labels fail") {
    for(const auto* source:{R"({"dock":{"pinned":[{"id":"fake","displayName":"File Explorer","executablePath":"S:\\Other\\explorer.exe"}]}})",
        R"({"dock":{"pinned":[{"id":"one","displayName":"Explorer","appUserModelId":"Microsoft.Windows.Explorer"},{"id":"two","displayName":"Explorer","appUserModelId":"Microsoft.Windows.Explorer"}]}})",
        R"({"dock":{"pinned":[{"id":"one","displayName":"label/with/path","appUserModelId":"Microsoft.Windows.Explorer"}]}})"})
        CHECK(!ResolveExplorerPinName(source,L"S:\\System\\explorer.exe"));
}
