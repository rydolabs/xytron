# Published-source CI sanity gate

This separate public branch uses only the already published source tree from
`nativeweb/autonomous-integration` in this **public** repository and a new
standalone, non-proprietary process-launch diagnostic.

Runs Windows/Linux C++11 Core/CLI regressions, installed Core SDK consumer,
and a generic executable-path regression (Win32 CreateProcess / POSIX launch).
The probe does **not** contain the private repository's CLI, SDK locks,
manifest validator, new DesktopShell or newer CEF/WebView2 code.

**A green run is proof that public GitHub-hosted Windows/Linux CI works for
these published artifacts, NOT verification of the latest private branch
`rydolabs/xytron_dev:nativeweb/cli-offline-core-workflow`.**
Never publish private source, personal tokens, artifact archives or secret
test fixtures to this branch. Do not merge this diagnostic into `main`.
