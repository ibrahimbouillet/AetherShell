// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace aether::test {
namespace {

struct RunState {
    const TestCase* current = nullptr;
    int failuresInCurrent = 0;
};

RunState& State() {
    static RunState state;
    return state;
}

}  // namespace

std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

Registrar::Registrar(const char* name, TestFunction function, const char* file, int line) {
    Registry().push_back(TestCase{name, function, file, line});
}

void ReportFailure(const char* file, int line, const std::string& message) {
    ++State().failuresInCurrent;
    std::cout << "    FAILED at " << file << "(" << line << "): " << message << "\n";
}

void Note(const std::string& message) {
    std::cout << "    note: " << message << "\n";
}

std::string NarrowForDisplay(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string result(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr,
                              nullptr);
    }
    return result;
}

std::filesystem::path MakeTempDirectory(std::string_view prefix) {
    static unsigned counter = 0;
    const auto base = std::filesystem::temp_directory_path() / "AetherShellTests";
    for (;;) {
        auto candidate = base / (std::string(prefix) + "-" + std::to_string(::GetCurrentProcessId()) + "-" +
                                 std::to_string(++counter));
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            std::filesystem::remove_all(candidate, ec);
        }
        std::filesystem::create_directories(candidate, ec);
        if (!ec) return candidate;
        if (counter > 1000) throw std::runtime_error("unable to create temporary test directory");
    }
}

}  // namespace aether::test

int main(int argc, char** argv) {
    using namespace aether::test;
    ::SetConsoleOutputCP(CP_UTF8);

    std::vector<std::string> filters;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            listOnly = true;
        } else {
            filters.emplace_back(argv[i]);
        }
    }

    auto matches = [&](const TestCase& test) {
        if (filters.empty()) return true;
        for (const auto& filter : filters) {
            if (std::string_view(test.name).find(filter) != std::string_view::npos) return true;
        }
        return false;
    };

    if (listOnly) {
        for (const auto& test : Registry()) {
            if (matches(test)) std::cout << test.name << "\n";
        }
        return 0;
    }

    int passed = 0;
    int failed = 0;
    const auto suiteStart = std::chrono::steady_clock::now();
    for (const auto& test : Registry()) {
        if (!matches(test)) continue;
        State().current = &test;
        State().failuresInCurrent = 0;
        const auto start = std::chrono::steady_clock::now();
        try {
            test.function();
        } catch (const RequireFailure&) {
            // Already reported.
        } catch (const std::exception& ex) {
            ReportFailure(test.file, test.line, std::string("unhandled exception: ") + ex.what());
        } catch (...) {
            ReportFailure(test.file, test.line, "unhandled non-standard exception");
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
        const bool ok = State().failuresInCurrent == 0;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << test.name << " (" << (elapsed.count() / 1000.0) << " ms)\n";
        (ok ? passed : failed)++;
    }
    const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                             suiteStart);
    std::cout << "\n" << passed << " passed, " << failed << " failed (" << total.count() << " ms)\n";
    if (passed + failed == 0) {
        std::cout << "No test matched the given filters.\n";
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
