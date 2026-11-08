// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "search/FileSearch.h"

#include "core/Strings.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>
#include <oledb.h>
#include <msdasc.h>
#include <searchapi.h>
#include <shlobj_core.h>
#include <knownfolders.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>
#include <initializer_list>
#include <memory>

namespace aether::search {
namespace {

// Constants of OLE DB that its headers only declare (defined by a library this project does not link).
constexpr GUID kDataInitialize = {0x2206CDB0, 0x19C1, 0x11D1, {0x89, 0xE0, 0x00, 0xC0, 0x4F, 0xD7, 0xA8, 0x29}};   // CLSID_MSDAINITIALIZE
constexpr GUID kDefaultDialect = {0xC8B521FB, 0x5CF3, 0x11CE, {0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D}};   // DBGUID_DEFAULT

bool EndsWithAny(std::wstring_view path, std::initializer_list<std::wstring_view> endings) {
    const auto lower = text::ToLower(path);
    return std::any_of(endings.begin(), endings.end(), [&](std::wstring_view ending) { return lower.ends_with(ending); });
}

// A string from COM's allocator.
struct CoTaskString {
    LPWSTR value = nullptr;
    ~CoTaskString() { ::CoTaskMemFree(value); }
    LPWSTR* put() { ::CoTaskMemFree(value); value = nullptr; return &value; }
};

constexpr std::size_t kColumns = 4;          // the item's URL, its file name, its item type, its folder's name
constexpr std::size_t kColumnChars = 2048;   // longer paths are left out rather than cut
struct Row {
    DBSTATUS status[kColumns];
    DBLENGTH length[kColumns];
    wchar_t value[kColumns][kColumnChars];
};

std::wstring ColumnOf(const Row& row, std::size_t column) {
    if (row.status[column] != DBSTATUS_S_OK) return {};
    return std::wstring(row.value[column], ::wcsnlen(row.value[column], kColumnChars));
}

// The index's System.ItemUrl ("file:C:/Users/..." or "file://server/share/...") as a Windows path. Its display path
// is translated into the display language (a French "Utilisateurs" for Users), so it cannot be opened: the URL holds
// the file's real place.
std::wstring PathOfUrl(std::wstring_view url) {
    constexpr std::wstring_view scheme = L"file:";
    if (url.size() <= scheme.size() || !text::EqualsIgnoreCase(url.substr(0, scheme.size()), scheme)) return {};
    url.remove_prefix(scheme.size());
    std::wstring path;
    if (url.starts_with(L"///")) {
        url.remove_prefix(3);
    } else if (url.starts_with(L"//")) {   // a network share
        url.remove_prefix(2);
        path = L"\\\\";
    }
    path += url;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

// The person's own places, as Start's search shows them: Desktop, Documents, Downloads, Pictures, Videos, Music and
// OneDrive, as one restriction of the index's query. Without any, the whole index.
std::wstring ScopeRestriction() {
    std::wstring scopes;
    for (const auto& folder : {FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads, FOLDERID_Pictures, FOLDERID_Videos, FOLDERID_Music,
                               FOLDERID_SkyDrive}) {
        PWSTR raw = nullptr;
        if (FAILED(::SHGetKnownFolderPath(folder, KF_FLAG_DONT_VERIFY, nullptr, &raw)) || !raw) { ::CoTaskMemFree(raw); continue; }
        std::wstring path(raw);
        ::CoTaskMemFree(raw);
        if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        std::replace(path.begin(), path.end(), L'\\', L'/');
        std::wstring quoted;   // a SQL string: its quotes doubled
        for (const wchar_t c : path) { quoted += c; if (c == L'\'') quoted += L'\''; }
        scopes += (scopes.empty() ? L"" : L" OR ") + std::wstring(L"SCOPE='file:") + quoted + L"'";
    }
    return scopes.empty() ? std::wstring(L"AND SCOPE='file:'") : L"AND (" + scopes + L")";
}

// What a person does not look for among their files: hidden folders (".git", ".venv"), package and cache folders.
bool InToolingFolder(std::wstring_view path) {
    const auto lower = text::ToLower(path);
    if (lower.find(L"\\.") != std::wstring::npos) return true;
    for (const auto* folder : {L"\\node_modules\\", L"\\site-packages\\", L"\\__pycache__\\", L"\\appdata\\"})
        if (lower.find(folder) != std::wstring::npos) return true;
    return false;
}

}  // namespace

std::wstring_view FileKindOf(std::wstring_view path, bool folder) noexcept {
    if (folder) return L"Folder";
    try {
        if (EndsWithAny(path, {L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".webm", L".m4v", L".mpg", L".mpeg", L".flv", L".3gp"})) return L"Video";
        if (EndsWithAny(path, {L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".heic", L".heif", L".tif", L".tiff", L".svg", L".raw", L".ico"})) return L"Picture";
        if (EndsWithAny(path, {L".mp3", L".wav", L".flac", L".m4a", L".aac", L".ogg", L".wma", L".opus"})) return L"Music";
        if (EndsWithAny(path, {L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".odt", L".ods", L".odp", L".txt", L".md", L".rtf", L".csv", L".epub"})) return L"Document";
        if (EndsWithAny(path, {L".zip", L".7z", L".rar", L".tar", L".gz", L".iso"})) return L"Archive";
    } catch (...) {
    }
    return L"File";
}

bool IsApplicationFile(std::wstring_view path) noexcept {
    try {
        // and Windows' own views of files (libraries, saved searches), which are not files to open from here
        return EndsWithAny(path, {L".lnk", L".url", L".exe", L".msc", L".cpl", L".appref-ms", L".website", L".library-ms", L".search-ms",
                                  L".searchconnector-ms"});
    } catch (...) {
        return true;
    }
}

apps::ApplicationInfo FileResult(std::wstring path, std::wstring name, bool folder, std::wstring_view parent) {
    apps::ApplicationInfo app;
    app.kind = apps::ApplicationKind::File;
    app.id = L"file:" + text::ToLower(path);
    app.displayName = name.empty() ? path : std::move(name);
    app.detail = std::wstring(FileKindOf(path, folder));
    if (!parent.empty()) app.detail += L" in " + std::wstring(parent);
    app.parsingName = std::move(path);
    return app;
}

Result<std::vector<apps::ApplicationInfo>> FindFiles(std::wstring_view query, std::size_t maxResults, const CancellationToken& cancel) {
    std::vector<apps::ApplicationInfo> found;
    const auto typed = text::Trim(query);
    if (typed.empty() || maxResults == 0) return found;
    if (typed.size() > 128) return MakeError(ErrorCode::InvalidArgument, "search query is longer than 128 UTF-16 units");
    try {
        winrt::com_ptr<ISearchManager> manager;
        HRESULT hr = ::CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(manager.put()));
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "Windows Search is unavailable");
        winrt::com_ptr<ISearchCatalogManager> catalog;
        hr = manager->GetCatalog(L"SystemIndex", catalog.put());
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "the Windows index is unavailable");
        winrt::com_ptr<ISearchQueryHelper> helper;
        hr = catalog->GetQueryHelper(helper.put());
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "the Windows index cannot be queried");
        const LCID locale = ::GetUserDefaultLCID();
        (void)helper->put_QueryContentLocale(locale);
        (void)helper->put_QueryKeywordLocale(locale);
        (void)helper->put_QueryMaxResults(static_cast<LONG>(std::min<std::size_t>(maxResults * 3, 300)));
        (void)helper->put_QuerySelectColumns(L"System.ItemUrl,System.FileName,System.ItemType,System.ItemFolderNameDisplay");
        (void)helper->put_QueryContentProperties(L"System.FileName");   // names, never what is inside the files
        const auto scope = ScopeRestriction();
        (void)helper->put_QueryWhereRestrictions(scope.c_str());
        (void)helper->put_QuerySorting(L"System.Search.Rank DESC");
        CoTaskString sql, connection;
        hr = helper->GenerateSQLFromUserQuery(std::wstring(typed).c_str(), sql.put());
        if (FAILED(hr) || !sql.value) return platform::ErrorFromHResult(FAILED(hr) ? hr : E_FAIL, "the search could not be put to the index");
        hr = helper->get_ConnectionString(connection.put());
        if (FAILED(hr) || !connection.value) return platform::ErrorFromHResult(FAILED(hr) ? hr : E_FAIL, "the index has no connection");
        if (cancel.IsCancelled()) return found;

        winrt::com_ptr<IDataInitialize> initializer;
        hr = ::CoCreateInstance(kDataInitialize, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(initializer.put()));
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "OLE DB is unavailable");
        winrt::com_ptr<IDBInitialize> source;
        hr = initializer->GetDataSource(nullptr, CLSCTX_INPROC_SERVER, connection.value, __uuidof(IDBInitialize), reinterpret_cast<IUnknown**>(source.put()));
        if (SUCCEEDED(hr)) hr = source->Initialize();
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "the index could not be opened");
        winrt::com_ptr<IDBCreateSession> sessions = source.try_as<IDBCreateSession>();
        if (!sessions) return MakeError(ErrorCode::Unavailable, "the index offers no session");
        winrt::com_ptr<IDBCreateCommand> session;
        hr = sessions->CreateSession(nullptr, __uuidof(IDBCreateCommand), reinterpret_cast<IUnknown**>(session.put()));
        winrt::com_ptr<ICommandText> command;
        if (SUCCEEDED(hr)) hr = session->CreateCommand(nullptr, __uuidof(ICommandText), reinterpret_cast<IUnknown**>(command.put()));
        if (SUCCEEDED(hr)) hr = command->SetCommandText(kDefaultDialect, sql.value);
        winrt::com_ptr<IRowset> rows;
        if (SUCCEEDED(hr)) hr = command->Execute(nullptr, __uuidof(IRowset), nullptr, nullptr, reinterpret_cast<IUnknown**>(rows.put()));
        if (FAILED(hr) || !rows) return platform::ErrorFromHResult(FAILED(hr) ? hr : E_FAIL, "the index could not be searched");
        winrt::com_ptr<IAccessor> accessor = rows.try_as<IAccessor>();
        if (!accessor) return MakeError(ErrorCode::Unavailable, "the index's rows cannot be read");
        std::array<DBBINDING, kColumns> bindings{};
        for (std::size_t i = 0; i < kColumns; ++i) {
            auto& binding = bindings[i];
            binding.iOrdinal = i + 1;
            binding.obValue = offsetof(Row, value) + i * kColumnChars * sizeof(wchar_t);
            binding.obLength = offsetof(Row, length) + i * sizeof(DBLENGTH);
            binding.obStatus = offsetof(Row, status) + i * sizeof(DBSTATUS);
            binding.dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
            binding.dwMemOwner = DBMEMOWNER_CLIENTOWNED;
            binding.eParamIO = DBPARAMIO_NOTPARAM;
            binding.cbMaxLen = kColumnChars * sizeof(wchar_t);
            binding.wType = DBTYPE_WSTR;
        }
        HACCESSOR handle = 0;
        hr = accessor->CreateAccessor(DBACCESSOR_ROWDATA, kColumns, bindings.data(), sizeof(Row), &handle, nullptr);
        if (FAILED(hr)) return platform::ErrorFromHResult(hr, "the index's rows cannot be read");
        struct Release { IAccessor* accessor; HACCESSOR handle; ~Release() { accessor->ReleaseAccessor(handle, nullptr); } } release{accessor.get(), handle};
        auto row = std::make_unique<Row>();
        for (;;) {
            if (cancel.IsCancelled()) break;
            std::array<HROW, 16> handles{};
            HROW* pointer = handles.data();
            DBCOUNTITEM count = 0;
            hr = rows->GetNextRows(DB_NULL_HCHAPTER, 0, static_cast<DBROWCOUNT>(handles.size()), &count, &pointer);
            for (DBCOUNTITEM i = 0; i < count; ++i) {
                if (found.size() >= maxResults || FAILED(rows->GetData(handles[i], handle, row.get()))) continue;
                auto path = PathOfUrl(ColumnOf(*row, 0));
                if (path.empty() || IsApplicationFile(path) || InToolingFolder(path)) continue;
                if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;   // the index can lag a deletion
                const auto type = ColumnOf(*row, 2);
                const bool folder = text::EqualsIgnoreCase(type, L"Directory");
                found.push_back(FileResult(std::move(path), ColumnOf(*row, 1), folder, ColumnOf(*row, 3)));
            }
            if (count) (void)rows->ReleaseRows(count, handles.data(), nullptr, nullptr, nullptr);
            if (hr != S_OK || count == 0 || found.size() >= maxResults) break;   // DB_S_ENDOFROWSET, or enough
        }
        return found;
    } catch (...) {
        return MakeError(ErrorCode::Internal, "the index could not be searched");
    }
}

}  // namespace aether::search
