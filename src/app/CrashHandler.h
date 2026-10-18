// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Last-chance crash reporting: writes a small minidump (no full memory, so no
// document contents) to the log folder and records the event in the log.
// At most a few dumps are kept. Dumps never leave the machine automatically.
#pragma once

#include <filesystem>

namespace aether::app {

void InstallCrashHandler(const std::filesystem::path& dumpDirectory, int maxDumps = 3);

}  // namespace aether::app
