// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Cancellation.h"
#include "core/Dispatcher.h"
#include "core/FileSystem.h"
#include "core/Result.h"
#include "core/Signal.h"
#include "core/Strings.h"
#include "core/WorkerPool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace aether;

AETHER_TEST("result: carries values and errors") {
    Result<int> good = 5;
    CHECK(good.ok());
    CHECK_EQ(good.value(), 5);
    CHECK_EQ(good.value_or(9), 5);

    Result<int> bad = MakeError(ErrorCode::Timeout, "too slow", 0x80072EE2);
    CHECK_FALSE(bad.ok());
    CHECK_EQ(bad.error().code, ErrorCode::Timeout);
    CHECK_EQ(bad.value_or(9), 9);
    CHECK(bad.error().Describe().find("Timeout: too slow") == 0);

    Result<void> done = Ok();
    CHECK(done.ok());
    Result<void> failed = MakeError(ErrorCode::Io, "disk");
    CHECK_EQ(failed.error().code, ErrorCode::Io);
}

AETHER_TEST("strings: utf8 and utf16 conversions round-trip") {
    const std::string utf8 = "Caf\xC3\xA9 \xF0\x9F\x8C\x8C";
    const std::wstring wide = text::Widen(utf8);
    CHECK_EQ(wide, std::wstring(L"Caf\u00e9 \U0001F30C"));
    CHECK_EQ(text::Narrow(wide), utf8);
    CHECK(text::IsValidUtf8(utf8));
    CHECK_FALSE(text::IsValidUtf8("\xC3"));
    CHECK_FALSE(text::IsValidUtf8("\xE2\x82"));
    CHECK(text::Widen("").empty());
}

AETHER_TEST("strings: case helpers and trimming") {
    CHECK_EQ(text::ToLower(L"\u00C9COLE Notepad"), std::wstring(L"\u00e9cole notepad"));
    CHECK(text::EqualsIgnoreCase(L"C:\\Windows", L"c:\\WINDOWS"));
    CHECK_FALSE(text::EqualsIgnoreCase(L"abc", L"abd"));
    CHECK(text::EqualsIgnoreCaseAscii("Content-Type", "content-type"));
    CHECK_EQ(text::Trim(std::string_view("  x y \r\n")), std::string_view("x y"));
    CHECK_EQ(text::Trim(std::wstring_view(L"\t z ")), std::wstring_view(L"z"));
    CHECK_EQ(text::SanitizeForLog("a\r\nb\x01"), std::string("a  b "));
}

AETHER_TEST("cancellation: callbacks run once and registrations unregister") {
    CancellationSource source;
    auto token = source.Token();
    int calls = 0;
    int removedCalls = 0;
    auto registration = token.OnCancel([&] { ++calls; });
    {
        auto removed = token.OnCancel([&] { ++removedCalls; });
    }
    CHECK_FALSE(token.IsCancelled());
    source.Cancel();
    source.Cancel();
    CHECK(token.IsCancelled());
    CHECK_EQ(calls, 1);
    CHECK_EQ(removedCalls, 0);

    int late = 0;
    auto lateRegistration = token.OnCancel([&] { ++late; });
    CHECK_EQ(late, 1);

    CancellationToken never;
    CHECK_FALSE(never.IsCancelled());
    CHECK_FALSE(never.CanBeCancelled());
}

AETHER_TEST("cancellation: registration destruction waits for a running callback") {
    CancellationSource source;
    std::atomic<bool> callbackStarted{false};
    std::atomic<bool> callbackFinished{false};
    auto registration = std::make_unique<CancellationToken::Registration>(source.Token().OnCancel([&] {
        callbackStarted = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        callbackFinished = true;
    }));
    std::thread canceller([&] { source.Cancel(); });
    while (!callbackStarted) std::this_thread::yield();
    registration.reset();  // must block until the callback completes
    CHECK(callbackFinished.load());
    canceller.join();
}

AETHER_TEST("worker pool: runs posted work and refuses work after shutdown") {
    std::atomic<int> counter{0};
    std::mutex mutex;
    std::condition_variable cv;
    {
        WorkerPool pool({3, L"test pool", ComApartment::MultiThreaded});
        for (int i = 0; i < 100; ++i) {
            CHECK(pool.Post([&] {
                if (++counter == 100) {
                    std::lock_guard lock(mutex);
                    cv.notify_all();
                }
            }));
        }
        std::unique_lock lock(mutex);
        CHECK(cv.wait_for(lock, std::chrono::seconds(10), [&] { return counter.load() == 100; }));
        lock.unlock();
        pool.Shutdown();
        CHECK(pool.IsShuttingDown());
        CHECK_FALSE(pool.Post([] {}));
    }
    CHECK_EQ(counter.load(), 100);
}

AETHER_TEST("worker pool: STA threads survive throwing tasks") {
    WorkerPool pool({1, L"sta pool", ComApartment::SingleThreaded});
    std::atomic<bool> ran{false};
    pool.Post([] { throw std::runtime_error("boom"); });
    pool.Post([&] { ran = true; });
    for (int i = 0; i < 500 && !ran; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(ran.load());
}

AETHER_TEST("dispatcher: manual dispatcher runs work in order") {
    ManualDispatcher dispatcher;
    std::string order;
    dispatcher.Post([&] {
        order += "a";
        dispatcher.Post([&] { order += "c"; });
    });
    dispatcher.Post([&] { order += "b"; });
    CHECK_EQ(dispatcher.RunPending(), 3u);
    CHECK_EQ(order, std::string("abc"));
    dispatcher.Close();
    CHECK_FALSE(dispatcher.Post([] {}));
}

AETHER_TEST("signal: connections disconnect on destruction") {
    Signal<int> signal;
    int sum = 0;
    auto first = signal.Connect([&](int v) { sum += v; });
    {
        auto second = signal.Connect([&](int v) { sum += 10 * v; });
        signal.Emit(1);
    }
    signal.Emit(2);
    CHECK_EQ(sum, 13);
    first.Disconnect();
    signal.Emit(100);
    CHECK_EQ(sum, 13);
    CHECK_EQ(signal.SlotCount(), 0u);
}

AETHER_TEST("filesystem: atomic writes replace content and reads enforce limits") {
    const auto dir = aether::test::MakeTempDirectory("core-fs");
    const auto file = dir / "nested" / "settings.json";
    REQUIRE(fs::WriteFileAtomic(file, "first").ok());
    REQUIRE(fs::WriteFileAtomic(file, "second version").ok());
    auto read = fs::ReadFile(file, 1024);
    REQUIRE(read.ok());
    CHECK_EQ(read.value(), std::string("second version"));

    auto tooLarge = fs::ReadFile(file, 4);
    CHECK_FALSE(tooLarge.ok());
    CHECK_EQ(tooLarge.error().code, ErrorCode::Integrity);

    auto missing = fs::ReadFile(dir / "missing.json", 1024);
    CHECK_EQ(missing.error().code, ErrorCode::NotFound);

    // No temporary files are left behind.
    int entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(dir / "nested")) ++entries;
    CHECK_EQ(entries, 1);

    auto space = fs::AvailableDiskSpace(file);
    CHECK(space.ok() && space.value() > 0);
    std::filesystem::remove_all(dir);
}

AETHER_TEST("filesystem: path containment rejects traversal") {
    const std::filesystem::path root = L"C:\\Users\\Example\\AppData\\Local\\Programs\\AetherShell";
    CHECK(fs::IsPathWithin(root, root / L"versions\\1.0.0\\AetherShell.exe"));
    CHECK_FALSE(fs::IsPathWithin(root, root / L"..\\Other\\evil.exe"));
    CHECK_FALSE(fs::IsPathWithin(root, L"D:\\elsewhere\\file.txt"));
    CHECK_FALSE(fs::IsPathWithin(root, root));
    CHECK_FALSE(fs::IsPathWithin(root, L"relative\\path"));
}
