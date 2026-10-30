// Native Apple WebKit API feasibility gate (not the Xytron backend).
// Must compile with AppleClang and the macOS SDK on both ARM64 and Intel.
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

@interface XytronProbeResources : NSObject <WKURLSchemeHandler>
@end

@implementation XytronProbeResources
- (void)webView:(WKWebView *)webView
    startURLSchemeTask:(id<WKURLSchemeTask>)task
{
    (void)webView;
    if (![[task.request.URL scheme] isEqualToString:@"app"] ||
        ![[task.request.URL host] isEqualToString:@"desktop"]) {
        NSError *error=[NSError errorWithDomain:@"XytronProbe"
                                          code:404 userInfo:nil];
        [task didFailWithError:error];
        return;
    }
    NSData *body=[@"<html><body>WKWebView</body></html>"
        dataUsingEncoding:NSUTF8StringEncoding];
    NSURLResponse *response=[[NSURLResponse alloc]
        initWithURL:task.request.URL MIMEType:@"text/html"
        expectedContentLength:(NSInteger)body.length
        textEncodingName:@"utf-8"];
    [task didReceiveResponse:response];
    [task didReceiveData:body];
    [task didFinish];
}
- (void)webView:(WKWebView *)webView
    stopURLSchemeTask:(id<WKURLSchemeTask>)task
{
    (void)webView;
    (void)task; // Synchronous in-memory response: no work remains to cancel.
}
@end

@interface XytronProbeBridge : NSObject <WKScriptMessageHandlerWithReply>
@end

@implementation XytronProbeBridge
- (void)userContentController:(WKUserContentController *)controller
    didReceiveScriptMessage:(WKScriptMessage *)message
    replyHandler:(void (^)(id _Nullable, NSString * _Nullable))replyHandler
{
    (void)controller;
    // Only a sketch of engine provenance: the production bridge also needs
    // active document session, trusted source and explicit permission checks.
    if (!message.frameInfo.isMainFrame) {
        replyHandler(nil, @"permission_denied");
        return;
    }
    replyHandler(@{ @"ok": @YES }, nil);
}
@end

int main()
{
    @autoreleasepool {
        WKWebViewConfiguration *config=[WKWebViewConfiguration new];
        XytronProbeResources *resources=[XytronProbeResources new];
        XytronProbeBridge *bridge=[XytronProbeBridge new];
        [config setURLSchemeHandler:resources forURLScheme:@"app"];
        [config.userContentController addScriptMessageHandlerWithReply:bridge
            contentWorld:[WKContentWorld pageWorld] name:@"xytron"];
        return 0;
    }
}
