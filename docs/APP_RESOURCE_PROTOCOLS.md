# Xytron app://, file:// and extensible resource protocols

Status: **CEF Linux/Windows and WebView2 Windows engine-backed app://
resources verified in real-browser E2E** (source d540df4a, 2026-10-14).
Full native-API authorization, packaged CLI run/publish, explicit file
access and custom user-defined schemes remain open. The primary product
is a standalone desktop app; embedded controls use the same policy.

## Product URL model (owner direction)

- `app://desktop/index.html` is the **target default packaged UI URL**.
  Per-feature isolated hosts, e.g. `app://camera/ui/panel.html`.
- `http://`, `https://` remain navigation/network protocols;
  they do NOT implicitly grant native Module access.
- `file://` is supported for explicitly chosen files and legacy
  development hosts; no generic unrestricted filesystem resource handler.
- Additional `foo://` schemes will be manifest-declared BEFORE browser
  environment initialization; runtime Modules only supply resource
  handlers for those predeclared schemes, not arbitrary late schemes.
- **Native Module registration != native invoke capability.** Permission
  must be checked per original frame/origin/app, method and object handle.

## Phase U0: implemented C++11 Core resource catalog

`nativeweb::AppResourceBundle` in `include/nativeweb/app_resources.hpp`
stores explicit byte resources and MIME types under canonical
`app://host/path` URLs, e.g.:

```cpp
nativeweb::AppResourceBundle resources;
resources.add("app://desktop/index.html", "text/html", html);
resources.add("app://desktop/assets/main.js", "application/javascript", js);
```

The first slice **rejects** HTTP/file/other schemes, noncanonical host
case, queries, fragments, percent-encoding, dot segments, backslashes,
empty path segments, userinfo/ports/control bytes and duplicate keys.
It never directly reads arbitrary local files. Asset bodies are bounded
at 16 MiB each. Core CTest `core.app_resources` covers positive and
negative cases. This resource catalog is now served through actual CEF/WebView2 app://
engine adapters when the host supplies the bundle via WebViewOptions.
The bridge checks an exact canonical app:// entry as well as the legacy
file:/// entry; it does NOT yet perform full per-method authorization.

## Phase U1/U2: implemented native browser resource handlers

1. **CEF:** register the standard secure `app` scheme in
   `CefApp::OnRegisterCustomSchemes` for browser and renderer processes;
   install a `CefSchemeHandlerFactory` before navigating. It must
   serve catalog bytes/MIME/status without access to undeclared paths.
2. **WebView2:** register custom `app` scheme via Environment options
   BEFORE `CreateCoreWebView2EnvironmentWithOptions`, then use
   request handlers to serve the same catalog (virtual HTTPS host mapping
   can be a separate compatibility fallback but does NOT make an app://
   URL work by itself).
3. **Security:** upgrade `trustedEntryDocument()` to use engine-supplied
   document identity and approved app:// origins plus exact entry path,
   without accidentally making file:// or arbitrary external sites
   trusted. Bind module permissions to actual native dispatch.
4. **Packaged app:** CLI `xytron new` and `run/build/publish` should
   expose app:// by default only after the real engine U1/U2 E2E is green.
   Preserve explicit file:// legacy host behavior until then.

## Test gates (U0–U3)

| ID | Test | Required proof |
| --- | --- | --- |
| A1-URL-01 | Known app:// page and relative JS/CSS/images | 3-engine rendered E2E |
| A1-URL-02 | Unknown asset, host isolation, duplicate keys | Core + 3-engine denied requests |
| A1-URL-03 | '../', percent encoded traversal, backslash, symlink path | Core + 3-engine deny |
| A1-URL-04 | Outside https/file frame tries resource/native access | 3-engine security negative E2E |
| A1-URL-05 | Exact file:// entry still works for legacy host | 3-engine regression E2E |
| A1-URL-06 | Invalid/reserved/delayed custom scheme declaration | Core + engine startup tests |
| A1-URL-07 | Only granted app:// methods/objects callable | 3-engine authorization E2E, NW-014 |
| A1-URL-08 | Reload, navigation, callback/Plugin unload revocation | lifecycle E2E + ASan |

U0 is NOT U1/U2 proof. Any future filesystem-backed adapter must use
real canonical file paths, symlink-resistant confinement and archive
path validation, not only URL-string filtering.

## Uploaded webview_util.zip

The uploaded utilities contain Callback/ThreadPool/V8ValueConverter/
VariantConverter concepts, **not any custom URL protocol scheme handler**
or a full resource-server implementation. They inform future CEF
callback/Value adapter work separately, not this URL loader's security.

## External implementation references

- CEF requires registration in `OnRegisterCustomSchemes` in *all*
  processes, plus browser-side scheme handler factory.
- WebView2 virtual host name mapping supports HTTPS host mapping, not
  app:// as the public origin; genuine app:// requires custom scheme
  environment registration and an actual resource response provider.

## U1 CEF integration — 3-engine source verification complete

The CEF app used by NativeWeb registers `app` as a standard, secure
custom scheme **in all CEF subprocesses**. Each CEF WebView with an
explicit `WebViewOptions::appResources` bundle gets its own request
context and scheme handler factory; no global resource map shared
across windows. The handler responds only with explicitly registered
in-memory resources, returns 404 for unknown keys, and never
opens filesystem paths. Before create(), the entry must be
present in the bundle. `trustedEntryDocument` now recognizes
canonical `app://` exact main document URLs *in addition to* the
preexisting exact file:/// legacy source; no remote HTTPS auto-grant.

The WebView2 adapter does **not** yet serve app://; its normal file://
workflow is unchanged. No CLI default change until both adapters
have a real end-to-end check. See NW-033.

## U2 WebView2 custom app scheme — Windows real E2E verified

WebView2 environment now receives `ICoreWebView2EnvironmentOptions4`
with `app` as an authority-bearing secure custom scheme before
environment creation. A `WebResourceRequested` filter and handler
returns explicit in-memory MIME/body resources (or 404), never
reading disk; remote cross-origin CORS grants are not emitted.
The existing WebMessageReceived native bridge still checks
host-supplied document identity. A new Windows WebView2 real
`browser.app_resource_lifecycle` E2E mirrors Linux/Windows CEF.

Because `CoreWebView2EnvironmentOptions4` and custom scheme
registration are runtime-dependent, the pinned SDK/CI must pass
without degrading back to file://. Full same-file iframe/method
authorization NW-014 remains a separate release blocker.

### U0 hardening / actual browser resource probes

When WebView::create() accepts an AppResourceBundle, it copies/snapshots
the developer-provided catalog before browser requests can run, avoiding
untrusted mutation or data races after create. Each resource is capped
at 16 MiB, and total bundle bytes at 64 MiB. C++11 CTest verifies copy
isolation and large-resource denial.

The real browser app:// E2E additionally performs a JS `fetch()`
against a known child fixture and checks status 200, MIME, exact byte
content and missing-resource status 404. The existing JS-to-native
plugin lifecycle and untrusted same-entry frame cases are still
required as part of the same scenario.

## Additional browser resource semantics tests — verified

For the packaged app:// E2E, both engines now load a complete
in-memory UI fixture: HTML, **relative external JS**, **relative
stylesheet**, **relative image (SVG)**, same-origin Fetch and a 404
for missing resources. The bundle deliberately includes
`app://camera/ui/panel.html` containing private fixture data; script
at `app://desktop` must not read its bytes (distinct origin and no
CORS access grant). The existing file:// legacy plugin E2E still
loads the same source fixtures from disk.

All tests must pass Windows CEF, Linux CEF and Windows WebView2
before labeling app:// usable in a generated desktop project.

## CLI default UI manifest declaration (not runnable yet)

The `xytron new desktop` project generator now writes a **declarative**
UI mapping:
`"ui": {"entry":"app://desktop/index.html","resources":"ui"}`.
This applies to all C++/Python/C# Main profiles and points to the
generated `ui/index.html` file. `xytron run` and `publish`
remain unimplemented, so the generator does **not** yet mount the
directory in a real desktop executable. The eventual runner must
read/validate resources, include only declared packaged bytes and use
the same isolated AppResourceBundle/engine APIs.

## 2026-10-14 definitive U0/U1/U2 app:// verification (source d540df4a)

The **real packaged `app://` resource protocol** is now implemented on
**Linux CEF, Windows CEF and Windows WebView2**, including:
- Explicit C++11 in-memory `AppResourceBundle` with strict canonical
  URLs, MIME types, duplicate rejection, 16 MiB per asset / 64 MiB per
  bundle; snapshot at `WebView::create` prevents caller mutation races.
- CEF secure/standard custom scheme, per-WebView isolated request
  context and scheme factory; WebView2 secure authority-bearing custom
  environment scheme and byte-stream `WebResourceRequested` responses.
- **Real browser E2E** serves HTML + relative JS/CSS/SVG assets, checks
  execution and MIME, Fetch success / undeclared asset 404, and denies
  reading private bytes from `app://camera` in a
  `app://desktop` page. Legacy exact-entry `file://` UI and
  JS/native plugin object lifecycle regressions remain tested.
- `xytron new desktop` CLI manifests for C++/Python/C# Main backends
  declare `ui.entry=app://desktop/index.html` and `ui.resources=ui`.
  This is **scaffolding only**; `xytron run/build/publish` and a
  packaged app shell remain unimplemented.

**All-green source and CI**
- [Actual 3-engine browser E2E #38525221064](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221064):
  Linux CEF, Windows CEF, Windows WebView2 PASS (both legacy file://
  plugin E2E and packaged app:// resource lifecycle E2E).
- [C++11 Core Regression #38525221058](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221058):
  Windows and Ubuntu PASS.
- [Native CLI #38525221109](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221109):
  Windows and Ubuntu PASS, including new generated manifest declaration.
- [Installed Core SDK #38525221089](https://github.com/nssang00/chatgpt-write-test/actions/runs/38525221089):
  Windows and Ubuntu PASS (Core-only, not engine-inclusive distribution).

**Not release-complete:** NW-014 **P0** native per-method, original frame,
navigation and native-object grants; file:// user-approved file picker
and filesystem confinement; late/duplicate declared custom scheme
registration; app resource bundle packaging/restore; actual desktop
shell, `xytron run/build/publish` and full embedded host controls.
`app://` page origin alone is NOT automatic native access.
