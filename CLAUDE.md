# Aether Shell — notes for Claude Code

The shared contract for every coding assistant is **`AGENTS.md`** (read it first), then `HANDOFF.md`
("NEXT SESSION START HERE"), applicable `PROJECT_SPEC.md` requirements, `TASKS.md` and `DECISIONS.md`.
Product requirements/acceptance belong to `PROJECT_SPEC.md`; source and scoped executed evidence
establish current behavior. Technical reference: `PROJECT_CONTEXT.md`; decisions:
`DECISIONS.md`; module work orders: `docs/specs/`. Those files are authoritative; this file only adds
Claude Code specifics.

## Claude Code specifics
- Project-scoped Graphify skill and PreToolUse hooks live in `.claude/` (local only, not committed).
  The hooks only nudge toward the graph; they never block reads.
- Large parallel work: use the Workflow tool in batches of three modules (implement → independent
  review), each agent restricted to its own module directory and private `build/wf-<module>` folder.
  Usage limits can interrupt runs; resume from the workflow cache instead of restarting.
- Do not read the whole knowledge vault or graph report; follow the targeted-retrieval order in
  `AGENTS.md` ("Efficient context gathering").

## graphify

Graphify is optional local navigation, not a product dependency or prerequisite for source reads.
Follow AGENTS.md's verified discovery state; on 2026-10-18 the launcher fails and its graph is stale.
Use targeted source/Markdown searches until a working local installation and refresh are verified.

When available, `graphify query "<question>"` explores dependencies; `explain <node-id>` and
`path <id-a> <id-b>` need exact IDs when a name is ambiguous. Confirm graph findings against source;
inferred edges and skipped macro-based tests cannot establish correctness. Optional `graphify update .`
refreshes local data after changes. Never commit `graphify-out/`, `.local/`, `.claude/` or `.obsidian/`.
No assistant-specific hook or generated instruction overrides the shared public-repository contract,
the product specification or independent verification duties.
