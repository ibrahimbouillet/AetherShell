<!-- Generated from the session's agent workflow; edit freely. -->
# Module specification: search (phase A)

Read `AGENTS.md` first; it states the rules every implementer follows. This is the work order that
was given to the implementing engineer. The real headers in the repository are authoritative where
they differ from names used here.

Module: search (directory src/search, target aether_search; deps: aether_core aether_diagnostics aether_platform). Include "applications/ApplicationTypes.h" as a header only; do NOT link aether_applications (its implementation is written in parallel).
Your files: the .h/.cpp files directly in src/search/ (NOT src/search/ui/**), src/search/CMakeLists.txt, tests/unit/search/**, docs/modules/search.md.
1. Normalization for matching: case folding plus accent removal (NormalizeString with NormalizationKD, drop nonspacing marks found with GetStringTypeW CT_CTYPE3 C3_NONSPACING/C3_DIACRITIC, then invariant lower case), keeping a map from normalized indices to original indices for highlighting.
2. Ranking (pure, deterministic): exact match > prefix of the name > prefix of a word (boundaries: space, - _ . and lower-to-upper camel boundaries in the original) > acronym ("vsc" matches "Visual Studio Code") > substring > in-order fuzzy subsequence with gap penalties; keywords and the executable file stem match with lower weight. Tie-breakers: shorter names, then ordinal order of the normalized name. Return highlight ranges in original display-name coordinates. Empty or whitespace query returns no results (the UI shows a hint). Maximum query length 128 characters.
3. SearchIndex: immutable snapshot built from vector<apps::ApplicationInfo> (dedupe by id) with precomputed normalized fields; thread-safe Query(text, maxResults); target under 2 ms for 2,000 apps on this machine (measure it).
4. SearchService: owns a WorkerPool; Rebuild(apps::IApplicationCatalog&) runs discovery and indexing in the background with cancellation (a newer rebuild cancels the older one); publishes the new index atomically (mutex-protected shared_ptr swap); notifies IndexReady through IUiDispatcher with count and duration; Query() always answers from the current snapshot and exposes IndexState {Empty, Building, Ready, Failed}. Search never executes anything the user types: results are only ApplicationInfo entries.
Tests: normalization (accents "Éditeur" ~ "editeur", case, compatibility forms), ranking order, acronyms, fuzzy, highlight ranges, tie-break stability, empty query, over-long query, surrogate pairs, queries concurrent with a rebuild, cancellation, and a performance test with 5,000 synthetic apps printing p50/p95 latency with TEST_NOTE (assert a generous bound such as p95 < 20 ms in Debug).
