// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/FileSystem.h"
#include "diagnostics/Logger.h"

using namespace aether;
using namespace aether::diag;

AETHER_TEST("logger: writes filtered, sanitized lines and rotates") {
    const auto dir = aether::test::MakeTempDirectory("logger");
    LoggerOptions options;
    options.directory = dir;
    options.baseName = L"test";
    options.maxFileBytes = 16 * 1024;
    options.maxRotatedFiles = 2;
    options.minimumSeverity = Severity::Info;
    options.writeToDebugger = false;
    Logger::Initialize(options);

    AETHER_LOG_DEBUG("tests", "hidden {}", 1);
    AETHER_LOG_INFO("tests", "visible {} {}", 42, "value");
    AETHER_LOG_WARN("tests", "{}", "forged\r\n2026-01-01 [ERROR] fake");
    Logger::Shutdown();

    auto contents = fs::ReadFile(dir / "test.log", 1 << 20);
    REQUIRE(contents.ok());
    CHECK(contents.value().find("hidden") == std::string::npos);
    CHECK(contents.value().find("[INFO ] [tests] visible 42 value") != std::string::npos);
    // The CR/LF in the message must not start a new log line.
    CHECK(contents.value().find("\r\n2026-01-01") == std::string::npos);

    Logger::Initialize(options);
    const std::string filler(1000, 'x');
    for (int i = 0; i < 100; ++i) AETHER_LOG_INFO("tests", "{} {}", i, filler);
    Logger::Shutdown();
    CHECK(std::filesystem::exists(dir / "test.1.log"));
    CHECK(std::filesystem::exists(dir / "test.2.log"));
    CHECK_FALSE(std::filesystem::exists(dir / "test.3.log"));
    CHECK(std::filesystem::file_size(dir / "test.log") <= options.maxFileBytes);
    std::filesystem::remove_all(dir);
}
