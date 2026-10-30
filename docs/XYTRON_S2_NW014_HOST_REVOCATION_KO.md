# S2 NW-014: Host-managed operation revoke (partial)

- Explicit managed-worker operation grants now use the Core `NativePermissionPolicy` exact method set.
- `WebView::revokeManagedOperation(pluginId, operation)` revokes one method; other explicitly granted operations remain callable.
- Permission is checked both before and after the ProcessBroker call. Revoked operations do not deliver late successful responses.
- `WebView::revokeManagedWorker(pluginId)` revokes all grants, unregisters methods and asynchronously stops the worker.
- Both APIs are for the trusted host on the WebView owner thread; never map them to untrusted JavaScript requests.
- Browser engine trust and main-document checks are still handled at the CEF and WebView2 backends. This additional policy does not claim to independently authenticate frameId, navigation or origin in `VerifiedFrameContext`.
- NW-014 full per-origin/frame capabilities and plugin signing/sandboxing remain **not implemented**.
- Regression: `core.native_permission_policy` and `core.managed_bridge`, including a real Python child process.
