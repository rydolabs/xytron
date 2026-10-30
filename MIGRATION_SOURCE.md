# Xytron source migration — 2026-10-31

## Source identity

- From: `nssang00/chatgpt-write-test`
- Source ref: `nativeweb/a1-xytron-public-api-compat-20261031`
- Exact source commit: `c3c208b20d160982508d0756a2c78bdc72e77cd7`
- Source review: [Draft PR #44](https://github.com/nssang00/chatgpt-write-test/pull/44)
- Destination: `rydolabs/xytron`
- Destination import branch: `import/xytron-a1-public-api-20261031`

## Imported snapshot

275 selected tracked source files, totaling 1,545,596 bytes, from the
source commit above were copied into this repository with **identical Git
blob SHAs and file modes**. Validation compared the full expected list to
the target Git tree: no missing file and no mismatch.

The 275 files include Xytron/NativeWeb C++ Core, public API, bindings,
CLI, CMake, SDK consumer, Core/plugin/browser/IPC test suites, product
documents and relevant GitHub Actions workflows.

The following 12 source-commit paths were intentionally not imported:
- `README.md` (replaced by the product-facing destination README)
- `FILE_LIST.txt`
- `HANDOFF.md`
- `HANDOFF_PACKAGE_README.md`
- `SNAPSHOT_SHA.txt`
- `chatgpt-push-demo-2026-10-07.txt`
- `assets/chatgpt-github-demo.svg`
- `tools/analyze_maptiler_style.py`
- `tools/record_maptiler_live.py`
- `.github/workflows/build-ol-disable-worker.yml`
- `.github/workflows/maptiler-live.yml`
- `.github/workflows/handoff-package.yml`

This is a **snapshot import**, not a full Git mirror or history transfer.
Unrelated branches, tags, issues, releases, and PRs were not copied.
The old source repository and its Draft PRs remain unchanged.

## Verification boundaries

Source commit `c3c208b` had 9/9 associated GitHub Actions workflows
completed successfully before import; the new destination CI must be
executed independently on the imported branch. A source-verification result
is not proof of successful distribution from a clean consumer machine.

## Release gates

**Scope A1 NO-GO.** The imported snapshot does not authorize a release,
a `main` merge of product code, a license choice, signing, runtime
distribution, or a security-policy relaxation.

Before first public binary distribution, complete and verify:
- SDK/runtime and browser engine restore and portable application publishing
- CEF sandbox/security boundaries and native menu/context menu acceptance
- Clean Windows/Linux host installation and run
- Redistributable CEF/WebView2/Python/.NET dependency and licensing review
- Original API/ABI compatibility and tests in this repository
- Explicit owner review/approval of any release

The root license/notice choices remain unresolved; none were invented
during import.
