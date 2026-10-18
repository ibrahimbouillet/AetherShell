// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "search/SearchService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <thread>

using namespace aether;
using namespace aether::search;
using namespace std::chrono_literals;

namespace {
apps::ApplicationInfo App(std::wstring id, std::wstring name) {
    apps::ApplicationInfo app; app.id = std::move(id); app.displayName = std::move(name); return app;
}
bool Ready(SearchService& service, ManualDispatcher& dispatcher) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (service.Current().state == IndexState::Building) {
        dispatcher.RunPending();
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return service.Current().state == IndexState::Ready;
}
}

AETHER_TEST("search: normalization strips accents and compatibility forms with original spans") {
    auto normalized = Normalize(L"Éditeur Ａ ﬁ");
    REQUIRE(normalized.ok());
    CHECK_EQ(normalized->text, std::wstring(L"editeur a fi"));
    CHECK_EQ(normalized->original.front(), (HighlightRange{0, 1}));
    CHECK_EQ(normalized->original.back(), (HighlightRange{10, 1}));
    auto emoji = Normalize(L"a\U0001F680b");
    REQUIRE(emoji.ok());
    CHECK_EQ(emoji->original[1], (HighlightRange{1, 2}));
    CHECK_EQ(emoji->original[2], (HighlightRange{1, 2}));
    CHECK_FALSE(Normalize(std::wstring(1, wchar_t{0xD800})).ok());
}

AETHER_TEST("search: exact prefix word acronym substring and fuzzy rank deterministically") {
    auto index = SearchIndex::Build({App(L"1", L"vsc"), App(L"2", L"vscode"), App(L"3", L"My vsc tool"),
        App(L"4", L"Visual Studio Code"), App(L"5", L"avscb"), App(L"6", L"Very Simple Calculator")});
    REQUIRE(index.ok());
    auto results = index.value()->Query(L"vsc", 20);
    REQUIRE(results.ok()); REQUIRE_EQ(results->size(), 6u);
    CHECK_EQ((*results)[0].application.id, std::wstring(L"1"));
    CHECK_EQ((*results)[1].application.id, std::wstring(L"2"));
    CHECK_EQ((*results)[2].application.id, std::wstring(L"3"));
    CHECK_EQ((*results)[3].application.id, std::wstring(L"4"));
    CHECK_EQ((*results)[4].application.id, std::wstring(L"6"));
    CHECK_EQ((*results)[5].application.id, std::wstring(L"5"));
    auto highlights = index.value()->Query(L"visual");
    REQUIRE(highlights.ok()); REQUIRE_FALSE(highlights->empty());
    CHECK_EQ(highlights->front().highlights, (std::vector<HighlightRange>{{0, 6}}));
    CHECK(index.value()->Query(L"   ")->empty());
    CHECK_FALSE(index.value()->Query(std::wstring(129, L'x')).ok());
}

AETHER_TEST("search: highlights compatibility expansion and surrogate pairs without splitting") {
    auto index = SearchIndex::Build({App(L"1", L"ﬁle \U0001F680 editor"), App(L"1", L"duplicate")});
    REQUIRE(index.ok()); CHECK_EQ(index.value()->Size(), 1u);
    auto fi = index.value()->Query(L"fi"); REQUIRE(fi.ok()); REQUIRE_EQ(fi->size(), 1u);
    CHECK_EQ(fi->front().highlights, (std::vector<HighlightRange>{{0, 1}}));
    auto emoji = index.value()->Query(L"\U0001F680"); REQUIRE(emoji.ok()); REQUIRE_EQ(emoji->size(), 1u);
    CHECK_EQ(emoji->front().highlights, (std::vector<HighlightRange>{{4, 2}}));
    auto camel = SearchIndex::Build({App(L"2", L"CodeEditor")}); REQUIRE(camel.ok());
    CHECK(camel.value()->Query(L"ce")->front().score == 7000);
}

AETHER_TEST("search: rebuild publishes on dispatcher and superseded index never wins") {
    ManualDispatcher dispatcher;
    SearchService service(dispatcher);
    int calls = 0;
    auto connection = service.IndexReady().Connect([&](const IndexReport& report) { ++calls; CHECK(dispatcher.IsUiThread()); CHECK_EQ(report.state, IndexState::Ready); });
    REQUIRE(service.Rebuild(std::vector{App(L"old", L"Old")}).ok());
    REQUIRE(service.Rebuild(std::vector{App(L"new", L"New")}).ok());
    REQUIRE(Ready(service, dispatcher));
    CHECK_EQ(calls, 1);
    CHECK(service.Query(L"old")->empty());
    CHECK_EQ(service.Query(L"new")->front().application.id, std::wstring(L"new"));
    std::atomic<int> queried = 0;
    std::jthread reader([&](std::stop_token stop) { while (!stop.stop_requested()) { auto results = service.Query(L"new"); if (results) ++queried; } });
    REQUIRE(service.Rebuild(std::vector{App(L"new2", L"Newest")}).ok());
    REQUIRE(Ready(service, dispatcher));
    reader.request_stop(); reader.join();
    CHECK(queried.load() > 0);
}

AETHER_TEST("search: synthetic 5000 application benchmark records p50 and p95") {
    std::vector<apps::ApplicationInfo> apps;
    for (int i = 0; i < 5000; ++i) apps.push_back(App(std::to_wstring(i), std::format(L"Synthetic Editor {}", i)));
    auto index = SearchIndex::Build(std::move(apps)); REQUIRE(index.ok());
    std::vector<double> milliseconds;
    for (int i = 0; i < 25; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        auto results = index.value()->Query(L"editor 42"); REQUIRE(results.ok());
        milliseconds.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    TEST_NOTE(std::format("5000 synthetic apps: p50 {:.3f} ms / p95 {:.3f} ms", milliseconds[12], milliseconds[23]));
    CHECK(milliseconds[23] < 20.0);  // scoped Debug engineering check, not a release-wide budget
}

AETHER_TEST("search: cancellation and unavailable dispatcher never leave a completed build pending") {
    CancellationSource source;source.Cancel();
    auto cancelled=SearchIndex::Build({App(L"1",L"Editor")},source.Token());
    REQUIRE_FALSE(cancelled.ok());CHECK_EQ(cancelled.error().code,ErrorCode::Cancelled);
    ManualDispatcher dispatcher;dispatcher.Close();SearchService service(dispatcher);
    REQUIRE(service.Rebuild(std::vector{App(L"1",L"Editor")}).ok());
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(service.Current().state==IndexState::Building && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
    CHECK_EQ(service.Current().state,IndexState::Failed);
    CHECK_FALSE(service.Query(std::wstring(129,L'x')).ok());
    CHECK_FALSE(service.Query(std::wstring(1,wchar_t{0xD800})).ok());
}
AETHER_TEST("search: 2000 application Release benchmark records the module target") {
    std::vector<apps::ApplicationInfo> apps;
    for(int i=0;i<2000;++i)apps.push_back(App(std::to_wstring(i),std::format(L"Synthetic Editor {}",i)));
    auto index=SearchIndex::Build(std::move(apps));REQUIRE(index.ok());
    std::vector<double> times;
    for(int i=0;i<35;++i){const auto start=std::chrono::steady_clock::now();auto results=index.value()->Query(L"editor 42");REQUIRE(results.ok());times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
    std::sort(times.begin(),times.end());
    TEST_NOTE(std::format("2000 synthetic apps: p50 {:.3f} ms / p95 {:.3f} ms",times[17],times[33]));
#ifdef NDEBUG
    CHECK(times[33]<2.0);
#endif
}
