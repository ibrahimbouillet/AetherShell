# Release process

The official destination is `ibrahimbouillet/AetherShell`. Keep the development checkout's original
Git history local; public source is an independently initialized, reviewed clean export (D18).
Never push private history or copy local logs, build output, tool configuration, keys or owner files.

Before publishing a milestone:

1. Complete the native Release `/W4 /WX` build and unit matrix. Run relevant desktop integration
   serially with safe own-window fixtures. Read TEST_STATUS and KNOWN_LIMITATIONS; do not relabel
   fake/read-only tests as real application/audio/startup/monitor qualification.
2. Inspect all publishable source/docs/resources. Preserve SPDX/MIT attribution and notices. Public
   captures must contain only synthetic project windows/backdrops, with no foreign overlap or blur
   samples. Exclude owner canvas, generated graphs, local tools/config, logs and debug artifacts.
3. Audit the actual Release app, launcher, updater and setup, including embedded payload bytes.
   `WINRT_NO_SOURCE_LOCATION` and filename-only RSDS/PDB metadata prevent private build-path leakage.
   An older Debug binary is not a publishable replacement for an audited Release build.
4. Commit a coherent reviewed source snapshot with the owner's public GitHub noreply identity on
   main, push to the official repository, and inspect the actual Windows CI result.
5. Package that source revision using `scripts/package.ps1`. Version/architecture come from built
   metadata, and payload/manifest/checksum digests come from real files. No invented hashes or sizes.
   CI uploads a manual-install setup and checksums for review; it omits unsigned manifests/PDBs.
6. If the owner-controlled trusted signing environment is configured privately, the package command
   signs the manifest and verifies the signature against the compiled public trust anchor. Do not
   print secrets or signing locations. If signing is unavailable, keep the unsigned manifest local;
   a draft manual-install alpha may contain only the audited setup/checksums and state that automatic
   updates are not available yet. Never weaken the production verifier to make a release pass.
7. Prepare release notes with actual version/source/test receipts, changes, limitations and install/
   rollback instructions. Create a draft prerelease for the alpha. Publish only under the owner's
   release authorization after assets/notes are concrete and reviewed; a source push is separate.

A complete authenticated update release contains correctly named setup asset(s), the exact signed
`AetherShell-<version>-manifest.json`, its `.sig` and checksums. Tags use `v<semver>`. Stable and
prerelease channels differ. Changing a signed manifest byte invalidates its signature; regenerate
and sign again after any asset/version/hash change.

After release, record the actual repository/commit/CI/release URLs and asset hashes in HANDOFF and
TEST_STATUS. Keep private signing configuration and local validation receipts out of those files.
Publishing an alpha does not waive remaining Version1 acceptance criteria.
