// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "applications/WindowEligibility.h"

using namespace aether::apps;

namespace {

WindowFacts EligibleWindow() {
    WindowFacts facts;
    facts.visible = true;
    facts.cloaked = false;
    facts.topLevel = true;
    facts.owned = false;
    facts.exStyle = WS_EX_WINDOWEDGE;
    facts.title = L"Document - Editor";
    facts.className = L"EditorWindowClass";
    facts.ownProcess = false;
    return facts;
}

}  // namespace

AETHER_TEST("eligibility: an ordinary top-level window is an application window") {
    CHECK_EQ(ClassifyWindow(EligibleWindow()), WindowExclusion::None);
    CHECK(IsEligibleAppWindow(EligibleWindow()));
}

AETHER_TEST("eligibility: hidden, cloaked, child and untitled windows are excluded") {
    auto hidden = EligibleWindow();
    hidden.visible = false;
    CHECK_EQ(ClassifyWindow(hidden), WindowExclusion::Hidden);

    auto cloaked = EligibleWindow();
    cloaked.cloaked = true;
    CHECK_EQ(ClassifyWindow(cloaked), WindowExclusion::Cloaked);

    auto child = EligibleWindow();
    child.topLevel = false;
    CHECK_EQ(ClassifyWindow(child), WindowExclusion::NotTopLevel);

    auto untitled = EligibleWindow();
    untitled.title = L"";
    CHECK_EQ(ClassifyWindow(untitled), WindowExclusion::Untitled);
    untitled.title = L"  \t ";
    CHECK_EQ(ClassifyWindow(untitled), WindowExclusion::Untitled);
}

AETHER_TEST("eligibility: our own windows are never listed") {
    auto own = EligibleWindow();
    own.ownProcess = true;
    own.exStyle |= WS_EX_APPWINDOW;  // even when they ask for a taskbar button
    CHECK_EQ(ClassifyWindow(own), WindowExclusion::OwnProcess);
}

AETHER_TEST("eligibility: shell windows are excluded by exact class name") {
    for (const wchar_t* shellClass : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        auto facts = EligibleWindow();
        facts.className = shellClass;
        CHECK(IsShellWindowClass(shellClass));
        CHECK_EQ(ClassifyWindow(facts), WindowExclusion::ShellWindow);
    }
    CHECK_FALSE(IsShellWindowClass(L"progman"));  // class names are matched exactly
    CHECK_FALSE(IsShellWindowClass(L"CabinetWClass"));
}

AETHER_TEST("eligibility: owned, tool and no-activate windows unless WS_EX_APPWINDOW") {
    auto owned = EligibleWindow();
    owned.owned = true;
    CHECK_EQ(ClassifyWindow(owned), WindowExclusion::Owned);
    owned.exStyle |= WS_EX_APPWINDOW;
    CHECK_EQ(ClassifyWindow(owned), WindowExclusion::None);

    auto tool = EligibleWindow();
    tool.exStyle |= WS_EX_TOOLWINDOW;
    CHECK_EQ(ClassifyWindow(tool), WindowExclusion::ToolWindow);
    tool.exStyle |= WS_EX_APPWINDOW;
    CHECK_EQ(ClassifyWindow(tool), WindowExclusion::None);

    auto noActivate = EligibleWindow();
    noActivate.exStyle |= WS_EX_NOACTIVATE;
    CHECK_EQ(ClassifyWindow(noActivate), WindowExclusion::NoActivate);
    noActivate.exStyle |= WS_EX_APPWINDOW;
    CHECK_EQ(ClassifyWindow(noActivate), WindowExclusion::None);
}

AETHER_TEST("eligibility: rules are applied in a fixed order") {
    WindowFacts everything;  // invisible, owned tool window of our own process without a title
    everything.ownProcess = true;
    everything.owned = true;
    everything.exStyle = WS_EX_TOOLWINDOW;
    CHECK_EQ(ClassifyWindow(everything), WindowExclusion::OwnProcess);
    everything.ownProcess = false;
    CHECK_EQ(ClassifyWindow(everything), WindowExclusion::Hidden);
    everything.visible = true;
    everything.topLevel = true;
    CHECK_EQ(ClassifyWindow(everything), WindowExclusion::Owned);
}
