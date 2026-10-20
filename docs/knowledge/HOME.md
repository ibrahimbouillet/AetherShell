# Aether Shell — knowledge home

Obsidian vault root: the `docs/` folder (registered as vault "docs"). It contains the ADRs
(`decisions/`), per-module references (`modules/`), module work orders (`specs/`) and these notes.

**Canonical project state lives at the repository root** (outside this vault): `AGENTS.md` (rules),
`HANDOFF.md` (start here), `PROJECT_SPEC.md` (objectives, requirements, acceptance and traceability),
`TASKS.md` (roadmap), `PROJECT_CONTEXT.md` (architecture and status),
`DECISIONS.md` (decision log). Open the repository root as a vault instead if you want those in
Obsidian too (exclude `build/`, `graphify-out/` and `.local/` in Settings → Files and links).

| Note | Use it for |
|---|---|
| [[MODULE_INDEX]] | Module → source folder, tests, status |
| [[TEST_STATUS]] | What was actually executed, passed, failed or not run |
| [[KNOWN_ISSUES]] | Limitations, open problems, tooling measurements |
| [[DEVELOPMENT_LOG]] | Short milestone history (details live in Git) |

Intended behavior: [PROJECT_SPEC](../../PROJECT_SPEC.md) and recorded decisions. Actual behavior:
source plus appropriately executed tests/observations, tied to the revision and configuration.
Code/spec disagreement is a gap to resolve, not permission to rewrite the requirement. Notes and
previous assistant claims are navigation aids; they do not establish completion.

Generated material (never edited by hand, never committed): `graphify-out/` (code graph) and
`.local/graphify-obsidian/` (Graphify's Obsidian export; open it as a separate vault).

Current D26 references: [quick controls](../modules/controlcenter.md),
[system controls](../modules/system_controls.md), [media/brightness](../modules/media_controls.md).
Read TEST_STATUS S-026 for executed checks and open qualification; D25 still holds publication.
