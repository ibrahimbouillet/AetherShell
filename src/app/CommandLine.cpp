// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/CommandLine.h"

#include "core/Strings.h"

#include <windows.h>
#include <shellapi.h>

#include <cwctype>

namespace aether::app {
namespace {

bool IsValidHealthToken(std::wstring_view token) {
    if (token.size() < 16 || token.size() > 64) return false;
    for (wchar_t c : token) {
        if (!std::iswxdigit(c)) return false;
    }
    return true;
}

}  // namespace

Result<CommandLine> ParseCommandLine(const std::vector<std::wstring>& arguments) {
    CommandLine result;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::wstring& arg = arguments[i];
        auto nextValue = [&]() -> std::optional<std::wstring> {
            if (i + 1 >= arguments.size()) return std::nullopt;
            return arguments[++i];
        };
        if (arg == L"--autostart") {
            result.autostart = true;
        } else if (arg == L"--version") {
            result.showVersion = true;
        } else if (arg == L"--settings") {
            result.action = StartupAction::OpenSettings;
        } else if (arg == L"--search") {
            result.action = StartupAction::OpenSearch;
        } else if (arg == L"--data-dir") {
            auto value = nextValue();
            if (!value || value->empty()) return MakeError(ErrorCode::InvalidArgument, "--data-dir needs a folder");
            result.dataDirectory = std::filesystem::path(*value);
        } else if (arg == L"--post-update-check") {
            auto value = nextValue();
            if (!value || !IsValidHealthToken(*value)) {
                return MakeError(ErrorCode::InvalidArgument, "--post-update-check needs a hexadecimal token");
            }
            result.healthToken = *value;
        } else {
            return MakeError(ErrorCode::InvalidArgument, "unknown argument: " + text::Narrow(arg));
        }
    }
    return result;
}

std::vector<std::wstring> ProcessArguments() {
    std::vector<std::wstring> arguments;
    int count = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (!argv) return arguments;
    for (int i = 1; i < count; ++i) arguments.emplace_back(argv[i]);
    ::LocalFree(argv);
    return arguments;
}

}  // namespace aether::app
