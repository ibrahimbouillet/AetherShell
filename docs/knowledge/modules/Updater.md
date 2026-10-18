# Updates, transaction and native setup — implemented, qualification pending

Requirements: [FR-018–FR-021](../../../PROJECT_SPEC.md). Tasks T5a/T9/T10/T13;
[executed scope S-009–S-011](../TEST_STATUS.md), [updates reference](../../modules/updates.md),
[install reference](../../modules/install.md) and [setup reference](../../modules/installer.md).

The trust chain is official GitHub Releases → ECDSA P-256 signed manifest → verified size/SHA-256
payload → independent updater verification → journaled stage/switch → health commit or rollback.
Nothing unverified is executed. The shared install library, stable launcher, updater and per-user
native setup exist; their presence is separate from production qualification. Design references:
[trust model](../../decisions/0004-update-trust-model.md) and
[layout/rollback](../../decisions/0005-installation-layout-and-rollback.md).

Historical update-service evidence is 117 cases; the current Release module reports 101 unit cases.
Current install evidence is 10 unit + 7 integration cases /WX using temporary roots, synthetic
resources, fake processes and injected registration. Native setup passed 5 cases with real embedded
payload hashes, installed-maintenance relocation and a constrained viewport in isolated roots.
The isolated app health/channel/shutdown case also passes.
Startup-consent registration, a production health/rollback cycle and an official owner-signed update remain
unverified. A relocated maintenance launch accepting handoff is not final-operation success.

`src/settings/ui` implements progress/error/verification presentation and update actions. The app
and existing Top Bar update indicator connect that flow; current editor/indicator regression passes.
A timer observes shared updater-launch completion if posting its UI notification fails; source review
found no actionable issue, but no live updater was executed for that review.
The private signing key stays outside the repository. Package scripts may prepare an unsigned
manifest locally; it must not be published or used as an automatic-update trust bypass. Final
package/payload/export privacy, public CI and approved release receipts remain pending. Filename-only
PDB records and disabled C++/WinRT source-location capture pass preliminary scans of four Release
binaries; older Debug binaries containing build paths must never be published.

Authorized delivery installed 0.1.0-alpha.1 using production setup with no launch; Installed Apps and
the launcher were present, startup absent. This observation is separate from automated tests. The
[official repository](https://github.com/ibrahimbouillet/AetherShell) exists but has no source push
or public CI receipt yet. Thirty-two Python release/package checks pass; final packaging is pending.
