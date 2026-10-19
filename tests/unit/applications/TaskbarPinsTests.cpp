// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Synthetic temporary shortcuts and injected pin lists only. Never enumerates the owner's taskbar,
// never reads the registry pin list (directory overrides select no registry source) and never
// launches a target.
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"
#include "applications/KnownApplications.h"
#include "applications/ShellItems.h"
#include "applications/TaskbarPins.h"

#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl_core.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

struct PinFixture {
    std::filesystem::path root = test::MakeTempDirectory("apps-pins");
    TaskbarPinImportOptions options;

    PinFixture() { options.directory = root / L"TaskBar"; std::filesystem::create_directory(options.directory); }
    ~PinFixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }

    std::filesystem::path Target(const wchar_t* name = L"FixtureApp.exe") const {
        const auto target = root / name;
        std::ofstream(target, std::ios::binary) << "synthetic non-executable fixture";
        return target;
    }

    std::filesystem::path Link(const wchar_t* name, const std::filesystem::path& target,
                               const std::wstring& arguments = {}, const std::wstring& aumid = {}) const {
        const auto path = options.directory / name;
        const auto made = shell::RunOnSta<void>([&]() -> Result<void> {
            winrt::com_ptr<IShellLinkW> link;
            HRESULT hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(link.put()));
            if (SUCCEEDED(hr) && !target.empty()) hr = link->SetPath(target.c_str());
            if (SUCCEEDED(hr)) hr = link->SetArguments(arguments.c_str());
            if (SUCCEEDED(hr) && !aumid.empty()) {
                winrt::com_ptr<IPropertyStore> store;
                hr = link->QueryInterface(IID_PPV_ARGS(store.put()));
                shell::PropVariant value;
                if (SUCCEEDED(hr)) hr = ::InitPropVariantFromString(aumid.c_str(), value.Put());
                if (SUCCEEDED(hr)) hr = store->SetValue(PKEY_AppUserModel_ID, value.Get());
                if (SUCCEEDED(hr)) hr = store->Commit();
            }
            winrt::com_ptr<IPersistFile> file;
            if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_PPV_ARGS(file.put()));
            if (SUCCEEDED(hr)) hr = file->Save(path.c_str(), TRUE);
            return SUCCEEDED(hr) ? Ok() : Result<void>(MakeError(ErrorCode::Platform, "fixture shortcut failed",
                                                                static_cast<std::uint32_t>(hr)));
        }, "fixture shortcut creation");
        REQUIRE(made.ok());
        return path;
    }
};

std::string Bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

ApplicationInfo CatalogEntry(std::wstring name, const std::filesystem::path& target,
                              std::wstring aumid = {}, std::wstring arguments = {}) {
    ApplicationInfo app;
    app.displayName = std::move(name);
    app.executablePath = target.native();
    app.appUserModelId = std::move(aumid);
    app.parsingName = app.appUserModelId;
    app.arguments = std::move(arguments);
    auto id = MakeApplicationId(app);
    REQUIRE(id.ok());
    app.id = std::move(id).value();
    return app;
}

}  // namespace

AETHER_TEST("taskbar pins: local shortcuts survive without running windows and are never rewritten") {
    PinFixture fixture;
    const auto firstTarget = fixture.Target();
    const auto secondTarget = fixture.Target(L"AnotherApp.exe");
    fixture.Link(L"Zeta.lnk", firstTarget);
    const auto first = fixture.Link(L"Alpha.lnk", firstTarget);
    fixture.Link(L"Middle.LNK", secondTarget);
    const auto before = Bytes(first);
    const auto timestamp = std::filesystem::last_write_time(first);

    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    REQUIRE_EQ(pins.value().size(), 2u);
    CHECK_EQ(pins.value()[0].displayName, std::wstring(L"Alpha"));
    CHECK_EQ(pins.value()[1].displayName, std::wstring(L"Middle"));
    CHECK(pins.value()[0].shortcutPath == first.native());
    // The shell resolves shortcut targets to long names; a temp folder can be in 8.3 form (CI runners).
    CHECK(MakeIdForExecutable(pins.value()[0].executablePath).value() == MakeIdForExecutable(firstTarget.native()).value());
    CHECK(pins.value()[0].id == MakeIdForExecutable(firstTarget.native()).value());
    CHECK(Bytes(first) == before);
    CHECK_EQ(std::filesystem::last_write_time(first), timestamp);
}

AETHER_TEST("taskbar pins: argument-bearing definitions retain distinct launch metadata") {
    PinFixture fixture;
    const auto target = fixture.Target();
    const auto first = fixture.Link(L"Profile One.lnk", target, L"--profile=one");
    const auto second = fixture.Link(L"Profile Two.lnk", target, L"--profile=two");
    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    REQUIRE_EQ(pins.value().size(), 2u);
    CHECK(pins.value()[0].id != pins.value()[1].id);
    CHECK_EQ(pins.value()[0].arguments, std::wstring(L"--profile=one"));
    CHECK_EQ(pins.value()[1].arguments, std::wstring(L"--profile=two"));
    CHECK(pins.value()[0].shortcutPath == first.native());
    CHECK(pins.value()[1].shortcutPath == second.native());
}

AETHER_TEST("taskbar pins: catalog reconciliation reuses identity and preserves the pinned shortcut") {
    PinFixture fixture;
    const auto target = fixture.Target();
    const auto shortcut = fixture.Link(L"Pinned Editor.lnk", target);
    fixture.options.catalog.push_back(CatalogEntry(L"Catalog Editor", target, L"Aether.Fixture.Editor"));
    fixture.options.catalog[0].keywords = L"synthetic catalog keyword";
    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    REQUIRE_EQ(pins.value().size(), 1u);
    const auto& pin = pins.value()[0];
    CHECK_EQ(pin.id, fixture.options.catalog[0].id);
    CHECK_EQ(pin.appUserModelId, std::wstring(L"Aether.Fixture.Editor"));
    CHECK_EQ(pin.displayName, std::wstring(L"Pinned Editor"));
    CHECK(pin.shortcutPath == shortcut.native());
    CHECK(MakeIdForExecutable(pin.executablePath).value() == MakeIdForExecutable(target.native()).value());
    CHECK_EQ(pin.keywords, fixture.options.catalog[0].keywords);
}

AETHER_TEST("taskbar pins: conflicting and ambiguous catalog profiles never replace pin identity") {
    PinFixture fixture;
    const auto target = fixture.Target();
    fixture.Link(L"Independent.lnk", target, {}, L"Aether.Profile.Independent");
    fixture.Link(L"Ambiguous.lnk", target, L"--shared");
    fixture.Link(L"Specific.lnk", target, L"--specific");
    fixture.options.catalog.push_back(CatalogEntry(L"Bare Executable", target));
    fixture.options.catalog.push_back(CatalogEntry(L"One", target, L"Aether.Profile.One"));
    fixture.options.catalog.push_back(CatalogEntry(L"First", target, L"Aether.Profile.First", L"--shared"));
    fixture.options.catalog.push_back(CatalogEntry(L"Second", target, L"Aether.Profile.Second", L"--shared"));
    fixture.options.catalog.push_back(CatalogEntry(L"Wrong Arguments", target, L"Aether.Profile.Wrong", L"--other"));
    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    REQUIRE_EQ(pins.value().size(), 3u);
    CHECK(pins.value()[0].id.starts_with(kShellIdPrefix));
    CHECK_EQ(pins.value()[0].arguments, std::wstring(L"--shared"));
    CHECK_EQ(pins.value()[1].id, MakeIdForAumid(L"Aether.Profile.Independent").value());
    CHECK_EQ(pins.value()[2].arguments, std::wstring(L"--specific"));
    CHECK(pins.value()[2].id.starts_with(kShellIdPrefix));
}

AETHER_TEST("taskbar pins: packaged AUMID shortcut imports without executable resolution") {
    PinFixture fixture;
    const std::wstring aumid = L"Aether.Fixture_123456789abcd!App";
    const auto shortcut = fixture.Link(L"Packaged Fixture.lnk", {}, {}, aumid);
    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    REQUIRE_EQ(pins.value().size(), 1u);
    CHECK_EQ(pins.value()[0].kind, ApplicationKind::Packaged);
    CHECK_EQ(pins.value()[0].id, MakeIdForAumid(aumid).value());
    CHECK_EQ(pins.value()[0].appUserModelId, aumid);
    CHECK_EQ(pins.value()[0].parsingName, aumid);
    CHECK(pins.value()[0].shortcutPath == shortcut.native());
}

AETHER_TEST("taskbar pins: malformed files URLs documents and nested folders are excluded") {
    PinFixture fixture;
    std::ofstream(fixture.options.directory / L"Broken.lnk") << "not a shell link";
    std::ofstream(fixture.options.directory / L"Site.url") << "[InternetShortcut]\nURL=https://example.invalid/\n";
    const auto document = fixture.root / L"Document.txt";
    std::ofstream(document) << "synthetic document";
    fixture.Link(L"Document.lnk", document);
    std::filesystem::create_directory(fixture.options.directory / L"Nested.lnk");
    const auto valid = fixture.Link(L"NestedSource.lnk", fixture.Target());
    std::filesystem::create_directory(fixture.options.directory / L"Subfolder");
    std::filesystem::rename(valid, fixture.options.directory / L"Subfolder" / L"Valid.lnk");
    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    CHECK(pins.value().empty());
}

AETHER_TEST("taskbar pins: missing folders stay absent and invalid roots fail without private details") {
    PinFixture fixture;
    fixture.options.directory = fixture.root / L"MissingPrivateMarker";
    auto missing = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(missing.ok());
    CHECK(missing.value().empty());
    CHECK_FALSE(std::filesystem::exists(fixture.options.directory));
    fixture.options.directory = L"relative-private-marker";
    auto relative = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE_FALSE(relative.ok());
    CHECK_EQ(relative.error().code, ErrorCode::InvalidArgument);
    CHECK(relative.error().message.find("private-marker") == std::string::npos);
    fixture.options.directory = fixture.Target();
    auto fileRoot = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE_FALSE(fileRoot.ok());
    CHECK_EQ(fileRoot.error().code, ErrorCode::Unsupported);
}

AETHER_TEST("taskbar pins: cancellation and enumeration limits return errors without partial pins") {
    PinFixture fixture;
    const auto target = fixture.Target();
    fixture.Link(L"One.lnk", target);
    fixture.Link(L"Two.lnk", target);
    fixture.options.maxShortcutFiles = 1;
    auto limited = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE_FALSE(limited.ok());
    CHECK_EQ(limited.error().code, ErrorCode::Unsupported);
    fixture.options.maxShortcutFiles = 0;
    CHECK_EQ(DiscoverTaskbarPins({}, fixture.options).error().code, ErrorCode::InvalidArgument);
    CancellationSource cancelled;
    cancelled.Cancel();
    CHECK_EQ(DiscoverTaskbarPins(cancelled.Token(), fixture.options).error().code, ErrorCode::Cancelled);
}

AETHER_TEST("taskbar pins: MTA callers are marshalled to the shell STA without launching") {
    PinFixture fixture;
    fixture.Link(L"Worker Fixture.lnk", fixture.Target());
    std::optional<Result<std::vector<ApplicationInfo>>> outcome;
    std::thread caller([&] {
        platform::ComApartment apartment(COINIT_MULTITHREADED);
        if (!apartment.IsUsable()) {
            outcome.emplace(MakeError(ErrorCode::Platform, "fixture MTA initialization failed"));
            return;
        }
        outcome.emplace(DiscoverTaskbarPins({}, fixture.options));
    });
    caller.join();
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->ok());
    CHECK_EQ(outcome->value().size(), 1u);
}

namespace {

constexpr wchar_t kPackagedAumid[] = L"Aether.Fixture_123456789abcd!App";

// A synthetic pin list: arbitrary framing bytes around UTF-16LE strings, like the real value but with
// no meaning attached to the framing.
std::vector<std::uint8_t> PinList(std::initializer_list<std::wstring_view> entries) {
    std::vector<std::uint8_t> bytes{0x00, 0x13, 0x07};
    for (const auto entry : entries) {
        bytes.insert(bytes.end(), {0xA5, 0x5A, 0x01});
        for (const wchar_t c : entry) {
            bytes.push_back(static_cast<std::uint8_t>(c & 0xFF));
            bytes.push_back(static_cast<std::uint8_t>((c >> 8) & 0xFF));
        }
        bytes.insert(bytes.end(), {0x00, 0x00});
    }
    return bytes;
}

std::vector<std::wstring> Names(const std::vector<ApplicationInfo>& pins) {
    std::vector<std::wstring> names;
    for (const auto& pin : pins) names.push_back(pin.displayName);
    return names;
}

ApplicationInfo PackagedCatalogEntry() {
    ApplicationInfo app;
    app.displayName = L"Fixture Packaged";
    app.kind = ApplicationKind::Packaged;
    app.appUserModelId = kPackagedAumid;
    app.parsingName = kPackagedAumid;
    app.id = MakeIdForAumid(kPackagedAumid).value();
    return app;
}

std::wstring LongPath(const std::filesystem::path& path) {
    const DWORD needed = ::GetLongPathNameW(path.c_str(), nullptr, 0);
    if (needed == 0) return path.native();
    std::wstring result(needed, L'\0');
    const DWORD written = ::GetLongPathNameW(path.c_str(), result.data(), needed);
    if (written == 0 || written >= needed) return path.native();
    result.resize(written);
    return result;
}

}  // namespace

AETHER_TEST("taskbar pins: an injected pin list gives Windows order, unmatched shortcuts follow by name") {
    PinFixture fixture;
    fixture.Link(L"Alpha.lnk", fixture.Target(L"AlphaApp.exe"));
    fixture.Link(L"Beta.lnk", fixture.Target(L"BetaApp.exe"));
    fixture.Link(L"Gamma.lnk", fixture.Target(L"GammaApp.exe"));
    int reads = 0;
    fixture.options.taskbandReader = [&reads]() -> Result<std::vector<std::uint8_t>> {
        ++reads;
        return PinList({L"GAMMA.lnk", L"Alpha.lnk"});
    };
    auto set = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(set.ok());
    CHECK_EQ(reads, 1);
    CHECK(set.value().windowsOrder);
    CHECK(Names(set.value().pins) == std::vector<std::wstring>({L"Gamma", L"Alpha", L"Beta"}));
    CHECK_EQ(set.value().fromTaskband, std::size_t{0});
    CHECK_EQ(set.value().skipped, std::size_t{0});
    // The plain wrapper returns the same pins.
    auto pins = DiscoverTaskbarPins({}, fixture.options);
    REQUIRE(pins.ok());
    CHECK(Names(pins.value()) == Names(set.value().pins));
}

AETHER_TEST("taskbar pins: a packaged pin known to the catalog is inserted at its pin-list position") {
    PinFixture fixture;
    fixture.Link(L"Alpha.lnk", fixture.Target(L"AlphaApp.exe"));
    fixture.Link(L"Beta.lnk", fixture.Target(L"BetaApp.exe"));
    fixture.options.catalog.push_back(PackagedCatalogEntry());
    fixture.options.taskbandReader = [] {
        return Result<std::vector<std::uint8_t>>(
            PinList({L"Beta.lnk", kPackagedAumid, L"Aether.NotInstalled_123456789abcd!App", L"Alpha.lnk"}));
    };
    auto set = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(set.ok());
    CHECK(Names(set.value().pins) == std::vector<std::wstring>({L"Beta", L"Fixture Packaged", L"Alpha"}));
    CHECK_EQ(set.value().fromTaskband, std::size_t{1});
    const auto& packaged = set.value().pins[1];
    CHECK_EQ(packaged.id, MakeIdForAumid(kPackagedAumid).value());
    CHECK_EQ(packaged.kind, ApplicationKind::Packaged);
    CHECK(packaged.shortcutPath.empty());
}

AETHER_TEST("taskbar pins: a packaged pin with its own shortcut yields one entry") {
    PinFixture fixture;
    const auto shortcut = fixture.Link(L"Packaged Fixture.lnk", {}, {}, kPackagedAumid);
    fixture.options.catalog.push_back(PackagedCatalogEntry());
    fixture.options.taskbandReader = [] {
        return Result<std::vector<std::uint8_t>>(PinList({kPackagedAumid, L"Packaged Fixture.lnk"}));
    };
    auto set = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(set.ok());
    REQUIRE_EQ(set.value().pins.size(), std::size_t{1});
    CHECK_EQ(set.value().pins[0].id, MakeIdForAumid(kPackagedAumid).value());
    CHECK(set.value().pins[0].shortcutPath == shortcut.native());
    CHECK(set.value().windowsOrder);
}

AETHER_TEST("taskbar pins: a failing or oversized pin list degrades to file-name order") {
    PinFixture fixture;
    fixture.Link(L"Beta.lnk", fixture.Target(L"BetaApp.exe"));
    fixture.Link(L"Alpha.lnk", fixture.Target(L"AlphaApp.exe"));
    fixture.options.taskbandReader = [] {
        return Result<std::vector<std::uint8_t>>(MakeError(ErrorCode::AccessDenied, "fixture reader failure"));
    };
    auto failed = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(failed.ok());
    CHECK_FALSE(failed.value().windowsOrder);
    CHECK(Names(failed.value().pins) == std::vector<std::wstring>({L"Alpha", L"Beta"}));

    fixture.options.taskbandReader = [] { return Result<std::vector<std::uint8_t>>(PinList({L"Beta.lnk"})); };
    fixture.options.maxTaskbandBytes = 16;
    auto oversized = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(oversized.ok());
    CHECK_FALSE(oversized.value().windowsOrder);
    CHECK(Names(oversized.value().pins) == std::vector<std::wstring>({L"Alpha", L"Beta"}));
}

AETHER_TEST("taskbar pins: isolated callers never read the user's pin list") {
    TaskbarPinImportOptions defaults;
    CHECK_EQ(SelectTaskbandSource(defaults), TaskbandSource::Registry);
    defaults.useTaskbandOrder = false;
    CHECK_EQ(SelectTaskbandSource(defaults), TaskbandSource::None);

    PinFixture fixture;
    fixture.Link(L"Alpha.lnk", fixture.Target(L"AlphaApp.exe"));
    CHECK_EQ(SelectTaskbandSource(fixture.options), TaskbandSource::None);  // directory override, no reader
    int reads = 0;
    fixture.options.taskbandReader = [&reads] {
        ++reads;
        return Result<std::vector<std::uint8_t>>(std::vector<std::uint8_t>{});
    };
    CHECK_EQ(SelectTaskbandSource(fixture.options), TaskbandSource::Injected);
    fixture.options.useTaskbandOrder = false;
    CHECK_EQ(SelectTaskbandSource(fixture.options), TaskbandSource::None);
    auto set = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(set.ok());
    CHECK_EQ(reads, 0);
    CHECK_FALSE(set.value().windowsOrder);
    CHECK_EQ(set.value().pins.size(), std::size_t{1});
}

AETHER_TEST("taskbar pins: File Explorer shortcuts become one canonical pin") {
    PinFixture fixture;
    const auto windows = fixture.root / L"Windows";
    std::filesystem::create_directory(windows);
    const auto explorer = windows / L"explorer.exe";
    std::ofstream(explorer, std::ios::binary) << "synthetic non-executable fixture";
    fixture.options.windowsDirectory = LongPath(windows);
    fixture.Link(L"File Explorer.lnk", explorer);
    fixture.Link(L"Explorer With Identity.lnk", explorer, {}, std::wstring(kFileExplorerAumid));
    fixture.Link(L"Projects.lnk", explorer, L"/e,C:\\Fixture\\Projects");
    auto set = DiscoverTaskbarPinSet({}, fixture.options);
    REQUIRE(set.ok());
    REQUIRE_EQ(set.value().pins.size(), std::size_t{2});
    const auto canonical = std::find_if(set.value().pins.begin(), set.value().pins.end(),
                                        [](const ApplicationInfo& pin) { return pin.id == FileExplorerId(); });
    REQUIRE(canonical != set.value().pins.end());
    CHECK_EQ(canonical->appUserModelId, std::wstring(kFileExplorerAumid));
    CHECK_EQ(canonical->parsingName, std::wstring(kFileExplorerAumid));
    CHECK(MakeIdForExecutable(canonical->executablePath).value() == MakeIdForExecutable(explorer.native()).value());
    CHECK(canonical->arguments.empty());
    const auto folder = std::find_if(set.value().pins.begin(), set.value().pins.end(),
                                     [](const ApplicationInfo& pin) { return pin.id != FileExplorerId(); });
    REQUIRE(folder != set.value().pins.end());
    CHECK_EQ(folder->arguments, std::wstring(L"/e,C:\\Fixture\\Projects"));
}
