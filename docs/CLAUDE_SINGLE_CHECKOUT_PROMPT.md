# Addendum — consolidate Aether Shell into one active working folder

This is an organization and continuity instruction, not a restart of the product or a replacement for the T42/T43 hover-preview brief. Claude performs the consolidation; Codex will review it afterward.

## Owner's objective

Keep ONE active project folder, preferably named AetherShell, shared by Claude and Codex in turn. Stop creating a separate checkout for each assistant or session. Preserve the official public repository, every useful local change and all private material without publishing it.

The current folders had different purposes: the original AetherShell preserves old private development history, AetherShell-public has the independently initialized public history, and AetherShell-codex-integration is a linked integration worktree. There are also historical Claude worktrees. These are not interchangeable copies.

## Audit before moving anything

Read AGENTS.md, HANDOFF.md, D18 and D39 in DECISIONS.md, and the focused hover implementation prompt. Inspect every checkout and git worktree list, including hidden worktrees, detached HEADs, local branches, staged/unstaged/untracked work and required ignored files. Keep inventories and actual local paths PRIVATE.

At the latest inspection, official main was at 9ffd0bc, three commits ahead of origin/main, including 2e21ff4 and 35746c8. This is a historical checkpoint: inspect fresh state and preserve ALL subsequent commits too. Never recreate the project from origin/main and lose local work.

Coordinate with any ongoing implementation before a move. Finish or suspend writes/builds cleanly; do not kill unrelated user applications. Do not reset, force-clean, overwrite one checkout with another, merge private history into the public repository, or delete a directory because its name looks obsolete.

## Consolidation procedure

Use the existing clean public-history checkout as the canonical Git lineage. Recover or retain any unique publishable work from other trees after inspecting it. Do not silently commit untested/incomplete product changes just to empty a worktree.

Before retiring the old private-history checkout or any unique worktree, make a local recovery archive outside the active project and VERIFY it is recoverable. Git history backups alone do not preserve uncommitted, untracked or ignored files: preserve those separately, including owner-owned local documentation/tool state. A recovery archive is a backup, not another active development project.

End with one ordinary active folder named AetherShell if safely achievable. Move/rename existing content rather than making another clone. Use Git-aware worktree move/remove/repair procedures; linked worktrees share Git administration. Never use forced worktree removal to discard unknown work. Check resolved absolute source/destination paths before recursive moves/removal, and use one native shell consistently.

Update affected local project/tool/Obsidian references and invalidate stale build caches as needed. Preserve the installed Alpha 2, its settings, pins, startup and recovery behavior. Retire redundant active folders/worktrees only after their useful content is retained and recovery checks pass. If some state cannot yet be safely consolidated, keep it preserved and explicitly report the remaining exception.

## Public/private and delivery rules

A local commit is not a publication. A commit or branch pushed to the public repository is not private, even if named private or WIP. Do not create a supposedly private commit/fork inside the public remote.

Keep private local artifacts in appropriately ignored local storage or outside the repository; keep old private Git history outside the public lineage. Gitignore does not remove already tracked files or erase history. Never commit credentials, private paths/metadata or unrelated captures.

Work in progress can remain staged/unstaged in the ONE canonical checkout between assistants. Record exactly what is unfinished, what was tested and where to resume; preserve it rather than resetting it. Make verified coherent LOCAL commits as appropriate. Push only reviewed publishable milestones when authorized; no push, tag or release for this consolidation batch.

Update the English AGENTS.md, HANDOFF.md, session prompts, TASKS.md and decision records so every assistant finds the same folder. Document the working-folder rule publicly without publishing private backup paths or inventories. Use sequential ownership or disjoint file ownership inside the same checkout; no new assistant-specific clones/worktrees unless the owner explicitly requests one.

Acceptance: one clear active folder; official origin intact; all local commits/changes preserved; private history recoverable and excluded from public Git; a focused Release /W4 /WX build works from the final location; installed Alpha 2 remains usable. Report final folder, retired worktrees, backup verification, commands/results and unresolved exceptions. Then continue the existing hover-preview brief without restarting the product.
