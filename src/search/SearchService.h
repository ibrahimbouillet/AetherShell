// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Dispatcher.h"
#include "core/Result.h"
#include "core/Signal.h"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace aether::search {

struct HighlightRange { std::size_t start = 0, length = 0; bool operator==(const HighlightRange&) const = default; };
struct NormalizedText {
    std::wstring text;
    std::vector<HighlightRange> original;  // one original UTF-16 span per normalized code unit
};
[[nodiscard]] Result<NormalizedText> Normalize(std::wstring_view text);

struct SearchResult {
    apps::ApplicationInfo application;
    int score = 0;
    std::vector<HighlightRange> highlights;
};

class SearchIndex {
public:
    static Result<std::shared_ptr<const SearchIndex>> Build(std::vector<apps::ApplicationInfo> applications,
                                                          const CancellationToken& cancel = {});
    [[nodiscard]] Result<std::vector<SearchResult>> Query(std::wstring_view query, std::size_t maxResults = 8) const;
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
private:
    struct Entry {
        apps::ApplicationInfo app;
        NormalizedText name, keywords, executable;
        std::vector<bool> wordStarts;
        std::wstring acronym;
        std::vector<std::size_t> acronymPositions;
        std::uint64_t characterMask = 0;
    };
    std::vector<Entry> entries_;
};

enum class IndexState { Empty, Building, Ready, Failed };
struct IndexReport {
    IndexState state = IndexState::Empty;
    std::size_t count = 0;
    std::chrono::milliseconds duration{};
};

class SearchService {
public:
    explicit SearchService(IUiDispatcher& dispatcher);
    ~SearchService();
    SearchService(const SearchService&) = delete;
    SearchService& operator=(const SearchService&) = delete;
    // catalog and dispatcher must outlive the service; no query text is executed or logged.
    Result<void> Rebuild(apps::IApplicationCatalog& catalog);
    Result<void> Rebuild(std::vector<apps::ApplicationInfo> applications);
    [[nodiscard]] Result<std::vector<SearchResult>> Query(std::wstring_view query, std::size_t maxResults = 8) const;
    [[nodiscard]] IndexReport Current() const;
    [[nodiscard]] Signal<const IndexReport&>& IndexReady();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace aether::search
