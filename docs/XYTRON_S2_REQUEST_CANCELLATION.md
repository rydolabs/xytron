# S2 JavaScript Request Cancellation (C++11)

New JS API: `xytron.invokeWithOptions(method, { timeoutMs, signal }, ...args)`.
`timeoutMs` is an optional integer from 1 to 60000; `signal` is an optional AbortSignal.
The existing `xytron.invoke()` remains unchanged.

On a timeout or abort, the JavaScript Promise rejects and an authenticated
`cancel` envelope is sent to the native bridge, keyed by request ID and bound
to the existing document-session token. CEF uses native process messages;
WebView2 uses the existing JSON messaging channel.

The Core records live inbound IDs, removes cancelled requests, skips worker
jobs which have not begun, and suppresses results after cancellation.
Navigation cancels every inbound JS request associated with the old document.
Unrelated calls continue to work.

Safety boundary: cancellation does not forcibly stop an already-running C++
method or a Python/.NET child process; side effects are not rolled back.
Separate process cancellation, safe worker restart, and native-to-JS future
per-request deadlines remain future work.

Tests: core bridge_message + bridge_async cancellation regressions, and
CEF Linux/Windows + WebView2 Windows real-browser AbortController, timeout
and follow-up math.add call. The real browser runner requires checkpoint
`request-cancel-verified` to avoid accepting a silent JS failure.
