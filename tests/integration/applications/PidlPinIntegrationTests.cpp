// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// A pinned shortcut that points to an Apps-folder item only by ID list (no file target), written to
// a temporary folder. It is imported through the Apps folder's File Explorer entry, which Windows
// provides on every supported version. Nothing is launched, and the user's pin folder and pin list
// are never read (directory override).
#include "AetherTest.h"

#include "applications/KnownApplications.h"
#include "applications/ShellItems.h"
#include "applications/TaskbarPins.h"
#include "platform/windows/ComApartment.h"

#include <windows.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace aether;
using namespace aether::apps;

namespace {

std::string Bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

Result<void> WriteIdListShortcut(const std::filesystem::path& path, std::wstring_view appsFolderName) {
    auto item = shell::CreateAppsFolderItem(appsFolderName);
    if (!item) return item.error();
    PIDLIST_ABSOLUTE raw = nullptr;
    HRESULT hr = ::SHGetIDListFromObject(item.value().get(), &raw);
    if (FAILED(hr) || !raw) return MakeError(ErrorCode::Platform, "fixture ID list", static_cast<std::uint32_t>(hr));
    const shell::UniqueIdList idList(raw);
    winrt::com_ptr<IShellLinkW> link;
    hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put()));
    if (SUCCEEDED(hr)) hr = link->SetIDList(idList.get());
    winrt::com_ptr<IPersistFile> file;
    if (SUCCEEDED(hr)) hr = link->QueryInterface(IID_PPV_ARGS(file.put()));
    if (SUCCEEDED(hr)) hr = file->Save(path.c_str(), TRUE);
    if (FAILED(hr)) return MakeError(ErrorCode::Platform, "fixture shortcut", static_cast<std::uint32_t>(hr));
    return Ok();
}

}  // namespace

AETHER_TEST("pins by ID list: an Apps-folder shortcut imports as the classified Apps-folder item") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    const auto root = test::MakeTempDirectory("apps-pidl-pins");
    const auto folder = root / L"TaskBar";
    std::filesystem::create_directory(folder);
    const auto shortcut = folder / L"Fixture Explorer Item.lnk";
    const auto written = WriteIdListShortcut(shortcut, kFileExplorerAumid);
    REQUIRE(written.ok());
    const auto before = Bytes(shortcut);
    const auto timestamp = std::filesystem::last_write_time(shortcut);

    TaskbarPinImportOptions options;
    options.directory = folder;
    auto set = DiscoverTaskbarPinSet({}, options);
    REQUIRE(set.ok());
    CHECK_EQ(set.value().pidlShortcuts, std::size_t{1});
    CHECK_EQ(set.value().skipped, std::size_t{0});
    REQUIRE_EQ(set.value().pins.size(), std::size_t{1});
    const auto& pin = set.value().pins[0];
    CHECK_EQ(pin.id, FileExplorerId());
    CHECK_EQ(pin.parsingName, std::wstring(kFileExplorerAumid));
    CHECK_EQ(pin.appUserModelId, std::wstring(kFileExplorerAumid));
    CHECK(pin.shortcutPath == shortcut.native());
    CHECK_FALSE(pin.executablePath.empty());  // canonical explorer.exe, so windows without an AUMID group
    CHECK_FALSE(pin.displayName.empty());
    CHECK(pin.arguments.empty());

    // Read-only import: the shortcut is neither resolved nor rewritten.
    CHECK(Bytes(shortcut) == before);
    CHECK_EQ(std::filesystem::last_write_time(shortcut), timestamp);

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
