# Native authorization foundation — implementation status

The internal C++11 NativePermissionPolicy can deny by default, require a
host-supplied verified main-frame identity and exact method grants, and revoke
on navigation. Its separate Core CTest covers the default deny behavior,
frame verification flags, main-frame-only, navigation epoch, wildcard and
prototype-name blocking, app-scoped grants and concurrent read/revoke.

**This is NOT wired to real CEF or WebView2 bridge message intake yet.**
Caller IDs/verification flags must be issued by the actual backend/host,
never parsed from JS-supplied payloads. The WebView engine must authenticate
frame, origin, app asset root and navigation; route object-id operations back
through method permissions; revoke permissions/tokens on reload/destroy.
Cross-origin child frames and backend-to-backend calls require further tests.

A green unit test for this Core primitive is NOT evidence that native API
access from an untrusted web page is blocked in a real browser.
A1-SEC-01..07 remain release blockers until full cross-engine negative
E2E and security review. See docs/RELEASE_TEST_MATRIX.md.

## Verified status and urgent integration gap

At source `27bf2489`, [Core Regression #38334609344](https://github.com/nssang00/chatgpt-write-test/actions/runs/38334609344) passed on Linux and Windows with `core.native_permission_policy`. It tests exact method grants, trusted verified main frame, frame/navigation ID mismatch, revoke and concurrent decisions.

**Outstanding P0 release blocker NW-014:** no proven wiring from CEF/WebView2 frame/origin identity to the policy and the actual native JS→C++ request dispatch. It does NOT yet prevent an untrusted page from calling a native method. When wiring, prohibit JS-supplied identity bits and authorize original object method ownership, not just the internal object operation name. Add negative real-browser tests on all three backends.

## Engine native message intake: initial trust gate

CEF's engine-owned originating CefFrame now must be main-frame and
equal to the originally hosted local file URL. WebView2's event-supplied
Source must equal both initial file entry and current top-level source.
The bridge refuses remote/unrelated file or redirected page requests
with permission_denied. A Core CTest exercises the pure file-entry guard.

Still outstanding for A1-SEC release gate: explicit app-root canonicalization,
same-origin frame isolation on WebView2, method-level grants, native-object
handle authorization, module-to-module permission evaluation, real negative
browser E2E for every denial and reload/navigation race. The exact URL
entry restriction is intentionally conservative and may limit existing
multi-page local-app navigation until explicit trust grants are implemented.

## Three-engine positive smoke after sender-document gate

[Browser plugin CI 38336744516](https://github.com/nssang00/chatgpt-write-test/actions/runs/38336744516)
passes on Linux CEF, Windows CEF and Windows WebView2 at
`7e45e72b`. This confirms that the exact local-entry trust gate
is compatible with the covered real JS→native plugin lifecycle.

**NW-014 stays a P0 release blocker**: no negative real-browser security
cases yet for remote page, arbitrary file, navigation, about:blank child
frames, WebView2 same-URL frame identity, native object authorization or
per-method grant revocation. The separate NativePermissionPolicy Core
primitive has not been fully integrated into actual browser dispatch.

## Additional negative actual-browser scenario (NW-019)

The trusted E2E entry now embeds a separate untrusted local-file iframe,
which attempts to call a real native checkpoint. CI fails if the side-effect
marker appears. CEF returns a permission_denied message to the originating
frame, while WebView2 may expose no subframe bridge at all. The host-native
request/response intake rejects all untrusted envelope types; this also
protects the C++→JS pending response path from arbitrary child responses.
This is additive coverage, not full same-file/same-origin WebView2 frame
proof, local path canonicalization, navigation race or method grant policy.

### Negative actual browser E2E: first working slice

[CI 38342611291](https://github.com/nssang00/chatgpt-write-test/actions/runs/38342611291):
Linux CEF, Windows CEF and Windows WebView2 PASS with an independent
local-file iframe that attempts a real native-side checkpoint; the
runner fails if the forbidden checkpoint is observed. The added
`nativeweb-security-probe-ready` handshake in `4ff17af2` prevents a
missing child document from falsely passing on timeout; corresponding
CI [38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515).
CEF and WebView2 also filter renderer-originated responses/error messages,
not only requests, using native engine source metadata. WebView2 must
not send denied child responses to the main page (NW-022).

This is **limited coverage**, not final security completion: same-URL
iframes, native object call authorization, source/canonical resource trust,
per-method grant revocation, backend-to-backend calls, navigation races
and remote network pages still have open negative tests in NW-014.

### Confirmed latest 3-engine security regression

Source `4ff17af2`: [E2E 38343177515](https://github.com/nssang00/chatgpt-write-test/actions/runs/38343177515)
**PASS Linux CEF / Windows CEF / Windows WebView2** with the required
child ready signal, forbidden-call side effect assertion, and normal
plugin lifecycle. NW-022 mitigation against WebView2 misdirected child
denial is in this passing source. This is **not** closure of NW-014.

## Same-entry reloaded as an untrusted frame — 2026-10-13

A secondary `iframe` loading the **identical** `plugin_bridge_test.html`
document is not entitled to main-frame native access even though its
file URL matches the app entry. The JS test detects child context,
posts a ready signal, attempts an unauthorized native checkpoint, and
reports whether the call was denied. Trusted main requires safe result
and logs `security-same-entry-iframe-blocked`. Runner fails on any
forbidden native checkpoint. Real-browser evidence:
[38354623946](https://github.com/nssang00/chatgpt-write-test/actions/runs/38354623946)
PASS Linux CEF + Windows CEF + Windows WebView2.

**NW-014 is NOT closed.** This checks one more frame boundary, not
the full method/permission/canonical trusted-root/navigation contract.
