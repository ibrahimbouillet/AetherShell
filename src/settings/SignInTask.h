// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <optional>
#include <string>

// D82: the per-user sign-in task in Windows' Task Scheduler, for the standard Run registration's adapter only.
// Blocking (Task Scheduler's COM service): call on a worker or in a short-lived process.
namespace aether::settings::detail {

Result<std::wstring> CurrentUserSid();
// The launcher the current user's sign-in task starts, or none when there is no such task.
Result<std::optional<std::wstring>> ReadWindowsSignInTask();
Result<void> WriteWindowsSignInTask(const std::wstring& launcher);
// Absent already is success.
Result<void> DeleteWindowsSignInTask();

}  // namespace aether::settings::detail
