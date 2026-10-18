// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "search/SearchService.h"

#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace aether::search {
namespace {

bool Separator(wchar_t c) { return std::iswspace(c) || c == L'-' || c == L'_' || c == L'.'; }
bool High(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool Low(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
bool HasType(wchar_t c, WORD flag) {
    WORD type = 0;
    return ::GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & flag) != 0;
}

struct Match {
    enum class Kind { Contiguous, Acronym, Fuzzy };
    int score = 0;
    std::size_t at = 0;
    Kind kind = Kind::Contiguous;
};
Match MatchName(std::wstring_view name, const std::vector<bool>& starts, std::wstring_view acronym,
                std::wstring_view query) {
    if (query.empty()) return {};
    const auto contiguous = [&](std::size_t at, int score) {
        return Match{score, at, Match::Kind::Contiguous};
    };
    if (name == query) return contiguous(0, 10000);
    if (name.starts_with(query)) return contiguous(0, 9000);
    const auto at = name.find(query);
    // Look at every occurrence: the first substring need not be the best word-boundary match.
    for (auto pos = at; pos != std::wstring::npos; pos = name.find(query, pos + 1)) {
        if (pos < starts.size() && starts[pos]) return contiguous(pos, 8000);
    }
    if (acronym.starts_with(query)) {
        return Match{7000, 0, Match::Kind::Acronym};
    }
    if (at != std::wstring::npos) return contiguous(at, 6000);
    std::size_t next = 0;
    std::size_t first = 0;
    bool matched = false;
    for (const wchar_t c : query) {
        const auto pos = name.find(c, next);
        if (pos == std::wstring::npos) return {};
        if (!matched) { first = pos; matched = true; }
        next = pos + 1;
    }
    const auto gaps = next - first - query.size();
    return Match{5000 - static_cast<int>(std::min<std::size_t>(gaps * 8 + first, 900)), first, Match::Kind::Fuzzy};
}

std::vector<HighlightRange> Highlights(const NormalizedText& text, const Match& match,
                                      std::wstring_view query, const std::vector<std::size_t>& acronymPositions) {
    std::vector<HighlightRange> spans;
    std::size_t next = match.at;
    for (std::size_t i = 0; i < query.size(); ++i) {
        const auto pos = match.kind == Match::Kind::Contiguous ? match.at + i :
                         match.kind == Match::Kind::Acronym ? acronymPositions[i] : text.text.find(query[i], next);
        next = pos + 1;
        const auto span = text.original[pos];
        if (!spans.empty() && span.start <= spans.back().start + spans.back().length) {
            spans.back().length = std::max(spans.back().start + spans.back().length, span.start + span.length) - spans.back().start;
        } else spans.push_back(span);
    }
    return spans;
}

}  // namespace

Result<NormalizedText> Normalize(std::wstring_view text) {
    try {
        NormalizedText result;
        for (std::size_t at = 0; at < text.size();) {
            const std::size_t width = High(text[at]) && at + 1 < text.size() && Low(text[at + 1]) ? 2 : 1;
            if ((High(text[at]) || Low(text[at])) && width == 1) return MakeError(ErrorCode::InvalidArgument, "invalid UTF-16 text");
            const auto count = ::NormalizeString(NormalizationKD, text.data() + at, static_cast<int>(width), nullptr, 0);
            if (count <= 0) return MakeError(ErrorCode::Platform, "Unicode normalization failed");
            std::wstring decomposed(static_cast<std::size_t>(count), L'\0');
            const int written = ::NormalizeString(NormalizationKD, text.data() + at, static_cast<int>(width), decomposed.data(), count);
            if (written <= 0) return MakeError(ErrorCode::Platform, "Unicode normalization failed");
            decomposed.resize(static_cast<std::size_t>(written));
            const int lowerCount = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, decomposed.data(), written, nullptr, 0, nullptr, nullptr, 0);
            if (lowerCount <= 0) return MakeError(ErrorCode::Platform, "Unicode case mapping failed");
            std::wstring lower(static_cast<std::size_t>(lowerCount), L'\0');
            if (!::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, decomposed.data(), written, lower.data(), lowerCount, nullptr, nullptr, 0)) return MakeError(ErrorCode::Platform, "Unicode case mapping failed");
            std::vector<WORD> types(lower.size());
            if (!::GetStringTypeW(CT_CTYPE3, lower.data(), lowerCount, types.data())) return MakeError(ErrorCode::Platform, "Unicode classification failed");
            for (std::size_t i = 0; i < lower.size(); ++i) {
                if ((types[i] & (C3_NONSPACING | C3_DIACRITIC)) != 0) continue;
                result.text += lower[i];
                result.original.push_back(HighlightRange{at, width});
            }
            at += width;
        }
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "search normalization failed"); }
}

Result<std::shared_ptr<const SearchIndex>> SearchIndex::Build(std::vector<apps::ApplicationInfo> applications,
                                                            const CancellationToken& cancel) {
    try {
        auto index = std::make_shared<SearchIndex>();
        std::unordered_set<std::wstring> seen;
        for (auto& app : applications) {
            if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "search indexing cancelled");
            if (app.id.empty() || app.displayName.empty() || !seen.insert(app.id).second) continue;
            auto name = Normalize(app.displayName);
            auto keywords = Normalize(app.keywords);
            const auto slash = app.executablePath.find_last_of(L"\\/");
            std::wstring stem = app.executablePath.substr(slash == std::wstring::npos ? 0 : slash + 1);
            if (const auto dot = stem.find_last_of(L'.'); dot != std::wstring::npos) stem.resize(dot);
            auto executable = Normalize(stem);
            if (!name || !keywords || !executable) return MakeError(ErrorCode::InvalidArgument, "invalid application search text");
            Entry entry{std::move(app), std::move(name.value()), std::move(keywords.value()), std::move(executable.value()), {}, {}, {}};
            entry.wordStarts.resize(entry.name.text.size());
            for (std::size_t i = 0; i < entry.name.text.size(); ++i) {
                const auto original = entry.name.original[i].start;
                // A compatibility expansion has only one word start at its first normalized unit.
                if (i && original == entry.name.original[i - 1].start) continue;
                entry.wordStarts[i] = original == 0 || Separator(entry.app.displayName[original - 1]) ||
                    (HasType(entry.app.displayName[original - 1], C1_LOWER) && HasType(entry.app.displayName[original], C1_UPPER));
                if (entry.wordStarts[i] && !Separator(entry.name.text[i])) {
                    entry.acronym += entry.name.text[i];
                    entry.acronymPositions.push_back(i);
                }
            }
            for (const auto* field : {&entry.name, &entry.keywords, &entry.executable})
                for (const wchar_t c : field->text) entry.characterMask |= std::uint64_t{1} << (c & 63);
            index->entries_.push_back(std::move(entry));
        }
        return std::shared_ptr<const SearchIndex>(std::move(index));
    } catch (...) { return MakeError(ErrorCode::Internal, "search index could not be built"); }
}

Result<std::vector<SearchResult>> SearchIndex::Query(std::wstring_view query, std::size_t maxResults) const {
    try {
        if (query.size() > 128) return MakeError(ErrorCode::InvalidArgument, "search query is longer than 128 UTF-16 units");
        while (!query.empty() && std::iswspace(query.front())) query.remove_prefix(1);
        while (!query.empty() && std::iswspace(query.back())) query.remove_suffix(1);
        std::vector<SearchResult> result;
        if (query.empty() || maxResults == 0) return result;
        auto normalized = Normalize(query);
        if (!normalized) return normalized.error();
        if (normalized->text.empty()) return result;
        struct Ranked { const Entry* entry; Match match; bool name; };
        std::vector<Ranked> ranked;
        std::uint64_t queryMask = 0;
        for (const wchar_t c : normalized->text) queryMask |= std::uint64_t{1} << (c & 63);
        for (const auto& entry : entries_) {
            // A conservative presence filter only rejects impossible matches (collisions are harmless).
            if ((entry.characterMask & queryMask) != queryMask) continue;
            auto match = MatchName(entry.name.text, entry.wordStarts, entry.acronym, normalized->text);
            bool nameMatch = true;
            for (const auto* field : {&entry.keywords, &entry.executable}) {
                if (field->text.empty() || match.score >= 7500) continue;
                auto extra = MatchName(field->text, {}, {}, normalized->text);
                extra.score -= 2500;
                if (extra.score > match.score) { match = std::move(extra); nameMatch = false; }
            }
            if (match.score > 0) ranked.push_back(Ranked{&entry, std::move(match), nameMatch});
        }
        const auto less = [](const Ranked& a, const Ranked& b) {
            if (a.match.score != b.match.score) return a.match.score > b.match.score;
            if (a.entry->app.displayName.size() != b.entry->app.displayName.size()) return a.entry->app.displayName.size() < b.entry->app.displayName.size();
            if (a.entry->name.text != b.entry->name.text) return a.entry->name.text < b.entry->name.text;
            return a.entry->app.id < b.entry->app.id;
        };
        const auto count = std::min(maxResults, ranked.size());
        std::partial_sort(ranked.begin(), ranked.begin() + count, ranked.end(), less);
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& item = ranked[i];
            result.push_back(SearchResult{item.entry->app, item.match.score,
                item.name ? Highlights(item.entry->name, item.match, normalized->text, item.entry->acronymPositions) : std::vector<HighlightRange>{}});
        }
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "search query failed"); }
}

struct SearchService::Impl {
    explicit Impl(IUiDispatcher& ui) : dispatcher(ui), worker(WorkerPool::Options{1, L"Aether search", ComApartment::MultiThreaded}) {}
    IUiDispatcher& dispatcher;
    WorkerPool worker;
    mutable std::mutex mutex;
    CancellationSource cancellation;
    std::uint64_t generation = 0;
    bool alive = true;
    IndexReport report;
    std::shared_ptr<const SearchIndex> index;
    Signal<const IndexReport&> ready;
};

SearchService::SearchService(IUiDispatcher& dispatcher) : impl_(std::make_shared<Impl>(dispatcher)) {}
SearchService::~SearchService() {
    CancellationSource cancellation;
    { std::lock_guard lock(impl_->mutex); impl_->alive = false; cancellation = impl_->cancellation; }
    cancellation.Cancel();
    impl_->worker.Shutdown();
}

Result<void> SearchService::Rebuild(apps::IApplicationCatalog& catalog) {
    try {
        CancellationSource previous, source;
        std::uint64_t generation;
        { std::lock_guard lock(impl_->mutex); previous = impl_->cancellation; impl_->cancellation = source;
          generation = ++impl_->generation; impl_->report.state = IndexState::Building; }
        previous.Cancel();
        const std::weak_ptr<Impl> weak = impl_;
        if (!impl_->worker.Post([weak, &catalog, source, generation] {
            if (source.IsCancelled()) return;
            auto state = weak.lock(); if (!state) return;
            const auto begin = std::chrono::steady_clock::now();
            Result<std::shared_ptr<const SearchIndex>> built = MakeError(ErrorCode::Internal, "catalog failed");
            try {
                auto apps = catalog.DiscoverApplications(source.Token());
                built = apps ? SearchIndex::Build(std::move(apps.value()), source.Token()) : Result<std::shared_ptr<const SearchIndex>>(apps.error());
            } catch (...) { AETHER_LOG_WARN("search", "catalog rebuild failed"); }
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
            const bool posted = state->dispatcher.Post([weak, source, generation, built = std::move(built), duration]() mutable {
                auto live = weak.lock(); if (!live || source.IsCancelled()) return;
                IndexReport report;
                { std::lock_guard lock(live->mutex); if (!live->alive || generation != live->generation) return;
                  if (built) live->index = std::move(built.value());
                  live->report = IndexReport{built ? IndexState::Ready : IndexState::Failed, live->index ? live->index->Size() : 0, duration};
                  report = live->report; }
                try { live->ready.Emit(report); } catch (...) { AETHER_LOG_WARN("search", "index listener failed"); }
            });
            if (!posted) {
                std::lock_guard lock(state->mutex);
                if (state->alive && generation == state->generation) state->report.state = IndexState::Failed;
            }
        })) return MakeError(ErrorCode::InvalidState, "search worker is stopped");
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "search rebuild failed"); }
}

Result<void> SearchService::Rebuild(std::vector<apps::ApplicationInfo> applications) {
    // This convenience overload is implemented as a direct index task, never a UI-thread build.
    try {
        CancellationSource previous, source;
        std::uint64_t generation;
        { std::lock_guard lock(impl_->mutex); previous = impl_->cancellation; impl_->cancellation = source;
          generation = ++impl_->generation; impl_->report.state = IndexState::Building; }
        previous.Cancel();
        const std::weak_ptr<Impl> weak = impl_;
        if (!impl_->worker.Post([weak, source, generation, applications = std::move(applications)]() mutable {
            if (source.IsCancelled()) return;
            const auto begin = std::chrono::steady_clock::now();
            auto built = SearchIndex::Build(std::move(applications), source.Token());
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
            if (auto state = weak.lock()) {
              const bool posted = state->dispatcher.Post([weak, source, generation, built = std::move(built), duration]() mutable {
                auto live = weak.lock(); if (!live || source.IsCancelled()) return;
                IndexReport report;
                { std::lock_guard lock(live->mutex); if (!live->alive || generation != live->generation) return;
                  if (built) live->index = std::move(built.value());
                  live->report = IndexReport{built ? IndexState::Ready : IndexState::Failed, live->index ? live->index->Size() : 0, duration}; report = live->report; }
                try { live->ready.Emit(report); } catch (...) { AETHER_LOG_WARN("search", "index listener failed"); }
              });
              if (!posted) { std::lock_guard lock(state->mutex); if (state->alive && generation == state->generation) state->report.state = IndexState::Failed; }
            }
        })) return MakeError(ErrorCode::InvalidState, "search worker is stopped");
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "search rebuild failed"); }
}

Result<std::vector<SearchResult>> SearchService::Query(std::wstring_view query, std::size_t limit) const {
    std::shared_ptr<const SearchIndex> index;
    { std::lock_guard lock(impl_->mutex); index = impl_->index; }
    if (index) return index->Query(query, limit);
    if (query.size() > 128) return MakeError(ErrorCode::InvalidArgument, "search query is longer than 128 UTF-16 units");
    auto normalized = Normalize(query);
    if (!normalized) return normalized.error();
    return std::vector<SearchResult>{};
}
IndexReport SearchService::Current() const { std::lock_guard lock(impl_->mutex); return impl_->report; }
Signal<const IndexReport&>& SearchService::IndexReady() { return impl_->ready; }

}  // namespace aether::search
