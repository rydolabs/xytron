# Real-browser plugin object integration test (2026-10-13)

This test is **not** the C++ fake-backend regression `plugin.async_lifetime`.
The browser creates an actual JavaScript native-object proxy and calls a
**separately built** shared-library plugin (`tests/plugin/object_factory_plugin.cpp`).

## What it verifies

- `xytron.plugin.open(77)` forwards a JavaScript argument through the CEF or
  WebView2 bridge and the size-gated C ABI object-factory callback.
- The proxy method calls `value()` and `add(5)` return `77` and `82`.
- C ABI `counter_failure` and invalid factory arguments reach rejected promises.
- A live object makes explicit unload return `plugin_busy`; the existing
  object still works during quiesce.
- `dispose()` revokes the handle; a later method call rejects.
- Unload succeeds once the object has been released.
- The browser process exits with status zero and prints all four checkpoints.

## Linux CEF

CEF must first be downloaded with the pinned `tools/bootstrap_linux.sh`
command and `CEF_ROOT` must point to its extracted directory. Configure the
CEF integration *as its own standalone CMake project*:

```bash
cmake -S tests/integration/cef -B build/cef -DCEF_ROOT=/path/to/cef
cmake --build build/cef --config Release -j 4
python3 tools/run_browser_plugin_e2e.py --engine cef --build-dir build/cef
# or: ctest --test-dir build/cef -R browser.plugin_object_lifecycle --output-on-failure
```

The runner uses `xvfb-run -a` if DISPLAY is unset; it checks for all required
artifacts before launch and fails on browser timeouts or missing checkpoints.
It does not silently skip when CEF or the fixture library is missing.

## Windows CEF

```powershell
cmake -S tests/integration/cef -B build/cef -DCEF_ROOT=C:/path/to/cef
cmake --build build/cef --config Release
py -3 tools/run_browser_plugin_e2e.py --engine cef --build-dir build/cef
```

## Windows WebView2

Use the pinned WebView2 SDK NuGet extraction and a machine with Evergreen
Runtime installed.

```powershell
cmake -S tests/integration/webview2 -B build/webview2 -DWEBVIEW2_ROOT=C:/path/to/sdk
cmake --build build/webview2 --config Release
py -3 tools/run_browser_plugin_e2e.py --engine webview2 --build-dir build/webview2
```

Both integration builds additionally expose
`ctest -R browser.plugin_object_lifecycle --output-on-failure`.  The test
page is shared at `tests/integration/cef/plugin_bridge_test.html`.

**Local verification scope:** Linux core regression tests and test-page syntax
validation can run without CEF. A successful CEF/WebView2 browser E2E result
must only be recorded after a real SDK-backed engine run.

## CI

`.github/workflows/browser-plugin-e2e.yml` builds and runs the browser plugin
E2E in Linux CEF, Windows CEF, and Windows WebView2 environments. This workflow has PASSED on Linux CEF, Windows CEF, and Windows WebView2
in [run 38322633033](https://github.com/nssang00/chatgpt-write-test/actions/runs/38322633033)
with source commit b2f1ced3. It still runs on matching PRs and can be
manually dispatched.

## Windows CEF archive extraction troubleshooting
Windows CEF CI previously downloaded and SHA1-verified the pinned 144 MB
minimal archive but stalled for 45 minutes during tar.exe -xjf extraction.
The Windows bootstrap now invokes tools/expand_cef_archive_windows.ps1,
which uses 7-Zip for the bzip2 and tar layers and deletes the intermediate tar.
A separate offline Windows fixture tests the extractor first and the workflow
caps real extraction at 20 minutes. The expected checksum and runtime layout
checks remain mandatory. Windows CEF archive extraction, full parallel-target build and real browser
E2E all PASSED in run 38322633033. See NW-008/NW-009 issue log.
