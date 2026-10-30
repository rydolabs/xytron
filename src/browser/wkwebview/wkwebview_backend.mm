#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#import <dispatch/dispatch.h>

#include "browser/wkwebview/wkwebview_backend.hpp"
#include "browser/wkwebview/wkwebview_bridge_script.hpp"
#include "browser/common/backend_registry.hpp"
#include "browser/common/document_session_token.hpp"
#include "browser/common/trusted_document.hpp"
#include "core/bridge_message.hpp"
#include "core/json_codec.hpp"
#include "nativeweb/app_resources.hpp"
#include "nativeweb/error.hpp"

#include <stdexcept>
#include <string>
#include <utility>

using nativeweb::detail::WkWebViewBackend;

namespace {
NSString* toString(const std::string& utf8)
{
    NSString* value=[[NSString alloc] initWithBytes:utf8.data()
        length:utf8.size() encoding:NSUTF8StringEncoding];
    if (!value) throw std::runtime_error("Invalid UTF-8 input");
    return value;
}

std::string toUtf8(NSString* value)
{
    const char* bytes=[value UTF8String];
    return bytes ? std::string(bytes) : std::string();
}

void requireMainThread()
{
    if (![NSThread isMainThread])
        throw nativeweb::Error("invalid_thread",
            "WKWebView create/load/destroy require the main thread");
}
} // namespace

@interface XytronWKResourceHandler : NSObject <WKURLSchemeHandler> {
@public
    std::shared_ptr<const nativeweb::AppResourceBundle> resources_;
}
@end

@implementation XytronWKResourceHandler
- (void)webView:(WKWebView*)view startURLSchemeTask:(id<WKURLSchemeTask>)task
{
    (void)view;
    const std::string url=toUtf8(task.request.URL.absoluteString);
    nativeweb::AppResource resource;
    if (!resources_ || !resources_->find(url,&resource)) {
        [task didFailWithError:[NSError errorWithDomain:@"Xytron.AppResource"
            code:404 userInfo:nil]];
        return;
    }
    NSData* bytes=[NSData dataWithBytes:resource.bytes.data()
                                 length:resource.bytes.size()];
    NSURLResponse* response=[[NSURLResponse alloc]
        initWithURL:task.request.URL
        MIMEType:toString(resource.mimeType)
        expectedContentLength:(NSInteger)resource.bytes.size()
        textEncodingName:nil];
    [task didReceiveResponse:response];
    [task didReceiveData:bytes];
    [task didFinish];
}
- (void)webView:(WKWebView*)view stopURLSchemeTask:(id<WKURLSchemeTask>)task
{
    (void)view; (void)task;
    // All packaged responses are currently synchronous and owned buffers.
}
@end

@interface XytronWKDelegate : NSObject <WKNavigationDelegate,
    WKScriptMessageHandlerWithReply>
@property(nonatomic,assign) WkWebViewBackend* backend;
@end

@implementation XytronWKDelegate
- (void)webView:(WKWebView*)view didStartProvisionalNavigation:(WKNavigation*)nav
{
    (void)nav;
    if (self.backend)
        self.backend->navigationStarted(toUtf8(view.URL.absoluteString));
}
- (void)webView:(WKWebView*)view didFinishNavigation:(WKNavigation*)nav
{
    (void)nav;
    if (self.backend)
        self.backend->navigationFinished(toUtf8(view.URL.absoluteString));
}
- (void)userContentController:(WKUserContentController*)controller
    didReceiveScriptMessage:(WKScriptMessage*)message
    replyHandler:(void (^)(id _Nullable, NSString* _Nullable))replyHandler
{
    (void)controller;
    if (!self.backend || ![message.body isKindOfClass:[NSString class]]) {
        replyHandler(nil, @"permission_denied"); return;
    }
    try {
        self.backend->scriptMessage(toUtf8((NSString*)message.body),
            toUtf8(message.frameInfo.request.URL.absoluteString),
            message.frameInfo.isMainFrame);
        replyHandler(@YES, nil);
    } catch (const std::exception&) {
        replyHandler(nil, @"invalid_message");
    }
}
@end

struct WkWebViewBackend::Impl
{
    __strong WKWebView* view = nil;
    __strong NSWindow* window = nil;
    __strong XytronWKDelegate* delegate = nil;
    __strong XytronWKResourceHandler* resources = nil;
};

namespace nativeweb { namespace detail {

WkWebViewBackend::WkWebViewBackend()
    : impl_(new Impl()), listener_(nullptr), navigation_(0), ready_(false),
      dispatchActive_(new std::atomic<bool>(false)), documentEpoch_(0)
{
}

WkWebViewBackend::~WkWebViewBackend() { destroy(); }

const char* WkWebViewBackend::engineName() const { return "wkwebview"; }
Engine WkWebViewBackend::engine() const { return Engine::WkWebView; }

Capabilities WkWebViewBackend::capabilities() const
{
    // First slice: JSON-framed copies, no zero-copy/shared memory promise.
    return Capabilities(static_cast<std::uint64_t>(Capability::Events) |
        static_cast<std::uint64_t>(Capability::Binary) |
        static_cast<std::uint64_t>(Capability::AsyncJavaScript));
}

void WkWebViewBackend::setListener(BrowserBackendListener* listener)
{
    listener_=listener;
}

void WkWebViewBackend::create(const BrowserCreateParams& params)
{
    requireMainThread();
    if (impl_->view) throw Error("webview_already_created", "Already created");
    if (params.source.compare(0,6,"app://")==0 &&
        (!params.appResources || !params.appResources->contains(params.source)))
        throw Error("app_resource_not_found", "Missing packaged entry");
    trustedEntry_=params.source;
    WKWebViewConfiguration* configuration=[WKWebViewConfiguration new];
    XytronWKResourceHandler* resources=[XytronWKResourceHandler new];
    resources->resources_=params.appResources;
    [configuration setURLSchemeHandler:resources forURLScheme:@"app"];
    XytronWKDelegate* delegate=[XytronWKDelegate new];
    delegate.backend=this;
    [configuration.userContentController addScriptMessageHandlerWithReply:delegate
        contentWorld:[WKContentWorld pageWorld] name:@"xytron"];
    WKUserScript* script=[[WKUserScript alloc] initWithSource:
        toString(kWkWebViewBridgeScript)
        injectionTime:WKUserScriptInjectionTimeAtDocumentStart
        forMainFrameOnly:YES];
    [configuration.userContentController addUserScript:script];
    NSRect bounds=NSMakeRect(0,0,800,600);
    WKWebView* view=[[WKWebView alloc] initWithFrame:bounds
        configuration:configuration];
    view.navigationDelegate=delegate;
    impl_->view=view;
    impl_->delegate=delegate;
    impl_->resources=resources;
    if (params.parent) {
        NSView* parent=(__bridge NSView*)params.parent;
        [parent addSubview:view];
    } else {
        NSWindow* window=[[NSWindow alloc] initWithContentRect:bounds
            styleMask:NSWindowStyleMaskTitled
            backing:NSBackingStoreBuffered defer:NO];
        [window setReleasedWhenClosed:NO];
        window.contentView=view;
        impl_->window=window;
    }
    dispatchActive_->store(true);
    if (listener_) listener_->onBrowserCreated();
    load(params.source);
}

void WkWebViewBackend::destroy()
{
    requireMainThread();
    if (!impl_->view) return;
    dispatchActive_->store(false);
    documentEpoch_.fetch_add(1);
    ready_=false;
    session_.clear();
    impl_->delegate.backend=nullptr;
    [impl_->view stopLoading];
    impl_->view.navigationDelegate=nil;
    [impl_->view.configuration.userContentController
        removeScriptMessageHandlerForName:@"xytron"
        contentWorld:[WKContentWorld pageWorld]];
    [impl_->view removeFromSuperview];
    [impl_->window close];
    impl_->view=nil;
    impl_->window=nil;
    impl_->delegate=nil;
    impl_->resources=nil;
    if (listener_) listener_->onBrowserClosed();
}

bool WkWebViewBackend::isCreated() const { return impl_->view!=nil; }

void WkWebViewBackend::load(const std::string& source)
{
    requireMainThread();
    if (!impl_->view) throw Error("engine_unavailable","WebView not created");
    NSURL* url=[NSURL URLWithString:toString(source)];
    if (!url) throw Error("invalid_url","Invalid WebView URL");
    [impl_->view loadRequest:[NSURLRequest requestWithURL:url]];
}

void WkWebViewBackend::reload()
{
    requireMainThread();
    if (impl_->view) [impl_->view reload];
}

void WkWebViewBackend::navigationStarted(const std::string& url)
{
    ready_=false;
    session_.clear();
    ++navigation_;
    documentEpoch_.fetch_add(1);
    if (listener_) listener_->onLoadStarted(url);
}

void WkWebViewBackend::navigationFinished(const std::string& url)
{
    if (!impl_->view) return;
    if (navigation_ && listener_ &&
        trustedEntryDocument(trustedEntry_,url,true)) {
        try { session_=createDocumentSessionToken(); }
        catch (...) { session_.clear(); }
        if (!session_.empty()) {
            ready_=true;
            VerifiedFrameContext ctx;
            ctx.frameId=1;
            ctx.navigation=navigation_;
            ctx.engineVerified=true;
            ctx.mainFrame=true;
            ctx.trustedContent=true;
            listener_->onTrustedFrameActivated(ctx);
            VariantDict offer;
            offer["type"]=std::string("nativeweb_session");
            offer["token"]=session_;
            postBridgeMessage(Any(offer));
        }
    }
    if (listener_) listener_->onLoadFinished(url);
}

void WkWebViewBackend::scriptMessage(const std::string& json,
    const std::string& callerUrl, bool mainFrame)
{
    requireMainThread();
    if (!ready_ || !mainFrame || !impl_->view || !navigation_ ||
        !trustedEntryDocument(trustedEntry_,callerUrl,mainFrame) ||
        toUtf8(impl_->view.URL.absoluteString)!=trustedEntry_)
        return;

    const Any value=jsonToAny(json);
    const VariantDict& fields=AnyCast<const VariantDict&>(value);
    const VariantDict::const_iterator token=fields.find("session");
    if (token==fields.end() || token->second.type()!=typeid(std::string) ||
        !matchesDocumentSessionToken(
            AnyCast<std::string>(token->second),session_))
        return;
    const BridgeMessage parsed=parseBridgeMessage(value);
    if (parsed.type==BridgeMessageType::Event &&
        (parsed.eventName.empty() || parsed.eventName.size()>256))
        return;
    if (!listener_) return;
    if (parsed.type==BridgeMessageType::Request ||
        parsed.type==BridgeMessageType::Event) {
        VerifiedFrameContext ctx;
        ctx.frameId=1;
        ctx.navigation=navigation_;
        ctx.engineVerified=true;
        ctx.mainFrame=true;
        ctx.trustedContent=true;
        listener_->onVerifiedBridgeMessage(value,ctx);
    } else {
        listener_->onBridgeMessage(value);
    }
}

void WkWebViewBackend::postBridgeMessage(const Any& message)
{
    // May run on a Core worker thread. Never read mutable Objective-C/Cocoa
    // handles until execution reaches the main queue. The immutable lifetime
    // token keeps this queued block safe even after the C++ object is freed.
    const std::shared_ptr<std::atomic<bool> > active=dispatchActive_;
    const std::uint64_t epoch=documentEpoch_.load();
    WkWebViewBackend* owner=this;
    const std::string json=anyToJson(message);
    NSString* jsonText=toString(json);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!active->load() || owner->documentEpoch_.load()!=epoch) return;
        WKWebView* view=owner->impl_->view;
        if (!view || !owner->impl_->delegate ||
            !owner->impl_->delegate.backend) return;
        NSData* data=[jsonText dataUsingEncoding:NSUTF8StringEncoding];
        id payload=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (!payload) return;
        [view callAsyncJavaScript:@"window.__xytronReceive(payload)"
            arguments:@{@"payload":payload}
            inFrame:nil inContentWorld:[WKContentWorld pageWorld]
            completionHandler:nil];
    });
}

void registerWkWebViewBackendFactory()
{
    registerBrowserBackendFactory(Engine::WkWebView,
        []() -> std::unique_ptr<BrowserBackend> {
            return std::unique_ptr<BrowserBackend>(new WkWebViewBackend());
        });
}

}} // namespace nativeweb::detail
