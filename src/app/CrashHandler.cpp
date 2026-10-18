// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/CrashHandler.h"

#include "diagnostics/Logger.h"

#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace aether::app {
namespace {

std::filesystem::path g_dumpDirectory;
int g_maxDumps = 3;

void PruneOldDumps() {
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> dumps;
    for (const auto& entry : std::filesystem::directory_iterator(g_dumpDirectory, ec)) {
        if (entry.path().extension() == L".dmp") dumps.push_back(entry);
    }
    std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b) {
        std::error_code e;
        return a.last_write_time(e) > b.last_write_time(e);
    });
    for (std::size_t i = static_cast<std::size_t>(std::max(0, g_maxDumps - 1)); i < dumps.size(); ++i) {
        std::filesystem::remove(dumps[i].path(), ec);
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info) {
    AETHER_LOG_CRITICAL("crash", "unhandled exception 0x{:08X}; writing minidump",
                        info && info->ExceptionRecord ? static_cast<unsigned>(info->ExceptionRecord->ExceptionCode) : 0u);
    PruneOldDumps();
    SYSTEMTIME now{};
    ::GetSystemTime(&now);
    wchar_t name[64];
    std::swprintf(name, 64, L"crash-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute, now.wSecond);
    const auto path = g_dumpDirectory / name;
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{::GetCurrentThreadId(), info, FALSE};
        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
                            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
                            info ? &exception : nullptr, nullptr, nullptr);
        ::CloseHandle(file);
    }
    diag::Logger::Shutdown();  // flush
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void InstallCrashHandler(const std::filesystem::path& dumpDirectory, int maxDumps) {
    g_dumpDirectory = dumpDirectory;
    g_maxDumps = std::max(1, maxDumps);
    ::SetUnhandledExceptionFilter(&OnUnhandledException);
}

}  // namespace aether::app
