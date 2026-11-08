# Search module

`src/search`, static target `aether_search`. Requirements FR-012/NFR-002.

D68: beyond the applications, the catalog brings Windows' tools (Control Panel items with French names and commands,
the listed System32 tools) and Windows' Settings pages (`SettingsPageApplications`, `ms-settings:` items of a fixed list).
`FileSearch` (`FindFiles`) asks Windows' search index for files and folders by name (`ISearchQueryHelper` and OLE DB,
read-only), scoped to the person's known folders; it reads each item's real path from `System.ItemUrl` (the display path
is translated), drops shortcuts, programs, Windows' views of files, hidden/package/AppData folders and anything that no
longer exists. `SearchService::QueryFiles` runs it on its own worker, answers only the latest request on the UI thread,
and ranks the files by name with the same matcher. Keywords and executable stems match only whole (no fuzzy letters).

`Normalize` uses Windows NFKD and invariant lower case, removes nonspacing/diacritic marks, and maps
normalized UTF-16 units back to whole original spans for highlights. Malformed surrogates are rejected.
`SearchIndex::Build` deduplicates IDs, caches normalized names/keywords/executable stems, word starts,
acronyms and character-presence masks, and returns an immutable thread-safe snapshot. Query accepts
at most128UTF-16 units; empty/whitespace input yields no matches.

Ranking: exact, name prefix, word prefix, acronym, substring, fuzzy subsequence; keyword/executable
matching has lower weight. Ties use name length, normalized ordinal name and ID. Highlights are
materialized only for selected result rows and never split surrogate pairs or compatibility spans.
Typed text is never executed.

`SearchService` owns a serialized worker and cancels superseded rebuild generations. Discovery and
indexing run there; publication and IndexReady notification use IUiDispatcher. Query uses the current
shared snapshot while a replacement builds. Failure to post completion produces Failed state.
The supplied catalogue must outlive any rebuild using it. Destruction cancels and joins the worker.

Initial /WX Debug verification:5cases pass in `build/wf-search2`;5000 synthetic apps measured
p50 6.238ms / p95 8.747ms after optimization (initial150ms and32ms versions failed the20ms check).
Settled Release verification: all seven cases pass, including cancellation/closed-dispatcher
and the 2000-app target. Recorded p50/p95: 5000 apps 1.409/1.962 ms; 2000 apps 0.512/0.601 ms. Whole-product budgets remain Q-001. The service received independent review;
closed-dispatcher, empty-index validation, cancellation and camel-case findings were corrected.
