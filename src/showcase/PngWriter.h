// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "showcase/FrameCompositor.h"
#include <string>
namespace aether::showcase {
// Lossless native PNG; caller initializes COM. Existing output is never replaced. No source paths
// or device/process metadata are embedded. Capture/review policy remains the caller's responsibility.
[[nodiscard]] Result<void> WritePngFrame(const std::wstring& outputFile, const RgbaFrameView& frame) noexcept;
}
