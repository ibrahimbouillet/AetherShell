// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/TaskbarReplacement.h"

#include "platform/windows/Win32Error.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace aether::platform {
namespace {
std::wstring Quote(std::wstring_view value) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { result.append(slashes * 2 + 1, L'\\'); result += c; }
        else { result.append(slashes, L'\\'); result += c; }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\'); result += L'"'; return result;
}
}

TaskbarGuardSession::~TaskbarGuardSession() { (void)Stop(); }
Result<std::unique_ptr<TaskbarGuardSession>> TaskbarGuardSession::Start(
    const std::filesystem::path& launcher, const std::filesystem::path& baselineFile,
    const CancellationToken& cancellation) try {
    if (cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled, "taskbar replacement cancelled");
    if (!launcher.is_absolute() || launcher.filename() != L"AetherShell.exe")
        return MakeError(ErrorCode::InvalidArgument, "taskbar guard launcher is invalid");
    if (!baselineFile.is_absolute()) return MakeError(ErrorCode::InvalidArgument, "taskbar baseline location is invalid");
    auto backend = MakeWindowsTaskbarBackend(); if (!backend) return backend.error();
    // D21: the baseline is on disk before the helper can hide anything.
    auto backup = PrepareTaskbarBaseline(**backend, baselineFile); if (!backup) return backup.error();
    // Nothing is hidden until the helper runs; drop the baseline if it never starts.
    struct DiscardBaseline {
        std::filesystem::path file; bool armed = true;
        ~DiscardBaseline() { if (armed) (void)RemoveTaskbarBaseline(file); }
    } discard{baselineFile};
    auto session = std::unique_ptr<TaskbarGuardSession>(new TaskbarGuardSession);
    session->backup_ = std::move(*backup);
    session->baselineFile_ = baselineFile;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    UniqueHandle parent(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, ::GetCurrentProcessId()));
    UniqueHandle ready(::CreateEventW(&security, TRUE, FALSE, nullptr));
    session->stop_.Reset(::CreateEventW(&security, TRUE, FALSE, nullptr));
    session->restoredEvent_.Reset(::CreateEventW(&security, TRUE, FALSE, nullptr));
    if (!parent || !ready || !session->stop_ || !session->restoredEvent_) return LastWin32Error("taskbar guard handles unavailable");
    SIZE_T size = 0; ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<std::byte> storage(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &size)) return LastWin32Error("taskbar guard attributes unavailable");
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST list; ~Attributes() { ::DeleteProcThreadAttributeList(list); } } cleanup{attributes};
    HANDLE handles[]{parent.Get(), ready.Get(), session->stop_.Get(), session->restoredEvent_.Get()};
    if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr))
        return LastWin32Error("taskbar guard inheritance unavailable");
    auto command = Quote(launcher.native()) + L" --taskbar-guard";
    for (const auto handle : handles) command += L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    if (!::CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, launcher.parent_path().c_str(), &startup.StartupInfo, &child))
        return LastWin32Error("taskbar guard could not start");
    discard.armed = false;  // from here the session's restoration logic owns the baseline file
    UniqueHandle thread(child.hThread); session->process_.Reset(child.hProcess);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const HANDLE waiting[]{ready.Get(), session->process_.Get()};
    while (!cancellation.IsCancelled() && std::chrono::steady_clock::now() < end) {
        const auto result = ::WaitForMultipleObjects(2, waiting, FALSE, 50);
        if (result == WAIT_OBJECT_0) return session;
        if (result != WAIT_TIMEOUT) return MakeError(ErrorCode::Unavailable, "taskbar guard could not take ownership");
    }
    return MakeError(cancellation.IsCancelled() ? ErrorCode::Cancelled : ErrorCode::Timeout, "taskbar guard readiness interrupted");
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar guard startup failed"); }

Result<bool> TaskbarGuardSession::Running() try {
    if (!process_) return false;
    const auto wait = ::WaitForSingleObject(process_.Get(), 0);
    if (wait == WAIT_TIMEOUT) return true;
    if (wait == WAIT_FAILED) return LastWin32Error("taskbar guard state unavailable");
    if (::WaitForSingleObject(restoredEvent_.Get(), 0) == WAIT_OBJECT_0) restored_ = true;
    if (!restored_) {
        auto backend = MakeWindowsTaskbarBackend(); if (!backend) return backend.error();
        auto restored = RestoreTaskbars(**backend, backup_); if (!restored) return restored.error();
        restored_ = true;
    }
    if (!baselineRemoved_) baselineRemoved_ = RemoveTaskbarBaseline(baselineFile_).ok();
    return false;
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar guard observation failed"); }

Result<void> TaskbarGuardSession::Stop() try {
    if (!process_) return Ok();
    ::SetEvent(stop_.Get());
    if (::WaitForSingleObject(process_.Get(), 5000) == WAIT_TIMEOUT)
        return MakeError(ErrorCode::Timeout, "taskbar guard is still restoring Windows");
    auto running = Running(); if (!running) return running.error();
    process_.Reset(); stop_.Reset(); restoredEvent_.Reset(); return Ok();
} catch (...) { return MakeError(ErrorCode::Internal, "taskbar guard shutdown failed"); }

}  // namespace aether::platform
