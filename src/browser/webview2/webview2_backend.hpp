#ifndef NATIVEWEB_WEBVIEW2_BACKEND_HPP_INCLUDED
#define NATIVEWEB_WEBVIEW2_BACKEND_HPP_INCLUDED

#if !defined(_WIN32)
#error NativeWeb WebView2 backend is Windows-only
#endif

#include "browser/common/browser_backend.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <string>

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

namespace nativeweb {
namespace detail {

class WebView2Backend : public BrowserBackend
{
public:
    WebView2Backend();
    ~WebView2Backend() override;

    const char* engineName() const override;
    Engine engine() const override;
    Capabilities capabilities() const override;

    void setListener(BrowserBackendListener* listener) override;
    void create(const BrowserCreateParams& params) override;
    void destroy() override;
    bool isCreated() const override;
    void load(const std::string& source) override;
    void reload() override;
    void postBridgeMessage(const Any& message) override;

    // Internal experimental Native -> main-document shared-buffer delivery.
    // Not a public WebView::Binary transport; used by isolated CI probe.
    bool postReadOnlySharedBufferProbe(const Binary& bytes, int requestId);
    // Cooperative release-ack accounting, only from a verified main-frame
    // event. This does NOT prove the renderer really released the buffer.
    bool acknowledgeReadOnlySharedBufferProbe(
        int requestId, const std::string& grant);
    std::size_t pendingReadOnlySharedBufferBytesForTest() const;
    std::size_t cumulativeReadOnlySharedBufferBytesForTest() const;
    // Internal process-failure probe: recovery is host initiated (Reload
    // for renderer failure; destroy/recreate for browser process failure).
    bool requestRendererCrashForTest();
    // Internal-only configuration for destructive browser-process tests.
    // Must be set before create; never used by public WebView options.
    void setIsolatedUserDataFolderForTest(const std::wstring& path);
    unsigned int rendererFailuresForTest() const;
    DWORD browserProcessIdForTest() const;
    // Environment-wide snapshot includes renderer/GPU/utility processes
    // sharing the WebView2 user data folder (excluding crashpad).
    struct ProcessMemorySnapshot
    {
        ProcessMemorySnapshot()
            : totalPrivateBytes(0), totalWorkingSetBytes(0),
              rendererPrivateBytes(0), processCount(0),
              rendererCount(0), sampledCount(0) {}
        std::size_t totalPrivateBytes;
        std::size_t totalWorkingSetBytes;
        std::size_t rendererPrivateBytes;
        unsigned int processCount;
        unsigned int rendererCount;
        unsigned int sampledCount;
    };
    bool processGroupMemoryForTest(ProcessMemorySnapshot* result) const;
    unsigned int browserProcessFailuresForTest() const;

private:
    struct CreateState
    {
        CreateState()
            : done(false),
              result(E_PENDING)
        {
        }

        bool done;
        HRESULT result;
    };

    struct DispatchTask
    {
        explicit DispatchTask(
            const std::function<void()>& value)
            : function(value)
        {
        }

        std::function<void()> function;
    };

    void requireUiThread() const;
    void createDispatchWindow();
    void destroyDispatchWindow();
    bool postToUi(const std::function<void()>& function);

    void finishControllerCreation(
        ICoreWebView2Controller* controller,
        CreateState* state);

    HRESULT installHandlers();
    HRESULT installBridgeScript(CreateState* state);

    void destroyOnUi();
    void navigateOnUi(const std::string& source);
    void postBridgeMessageOnUi(const Any& message);

    void notifyLoadStarted(const std::wstring& source);
    void notifyLoadFinished();

    bool pumpUntil(CreateState& state, DWORD timeoutMs);

    static LRESULT CALLBACK dispatchWindowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    static std::wstring utf8ToWide(const std::string& value);
    static std::string wideToUtf8(const wchar_t* value);

    BrowserBackendListener* listener_;
    HWND parent_;
    HWND dispatchWindow_;
    DWORD uiThreadId_;
    bool comInitialized_;
    std::atomic<bool> created_;
    std::atomic<bool> closing_;
    std::string source_;
    std::string trustedEntryUrl_;
    std::wstring isolatedUserDataFolderForTest_;
    std::uint64_t navigation_ = 0;
    bool mainFrameReady_ = false;
    unsigned int rendererFailures_ = 0;
    unsigned int browserProcessFailures_ = 0;
    std::string documentSessionToken_;
    struct ProbeGrant
    {
        std::size_t bytes;
        std::string nonce;
    };
    // A unique per-transfer grant binds ACKs to this specific allocation,
    // not just the request id (which can be reused after navigation).
    std::map<int, ProbeGrant> sharedProbePending_;
    std::size_t sharedProbePendingBytes_ = 0;
    // Nonrefundable per-document budget, even if a renderer forges ACKs.
    std::size_t sharedProbeCumulativeBytes_ = 0;
    static const std::size_t kSharedProbeMaxDocumentBytes = 32u * 1024u * 1024u;
    // Intentionally small, since JS can omit/falsify release ack.
    static const std::size_t kSharedProbeMaxPendingBytes = 2u * 1024u * 1024u;
    static const std::size_t kSharedProbeMaxPendingRequests = 4u;
    std::shared_ptr<const AppResourceBundle> appResources_;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;

    EventRegistrationToken webResourceRequestedToken_;
    EventRegistrationToken webMessageToken_;
    EventRegistrationToken navigationStartingToken_;
    EventRegistrationToken navigationCompletedToken_;
    EventRegistrationToken processFailedToken_;
    bool webResourceRequestedRegistered_;
    bool webMessageRegistered_;
    bool navigationStartingRegistered_;
    bool navigationCompletedRegistered_;
    bool processFailedRegistered_;
};

bool isWebView2RuntimeAvailable();
void registerWebView2BackendFactory();

} // namespace detail
} // namespace nativeweb

#endif
