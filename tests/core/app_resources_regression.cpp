#include "nativeweb/app_resources.hpp"
#include "nativeweb/error.hpp"
#include <iostream>
#include <string>

using nativeweb::AppResource;
using nativeweb::AppResourceBundle;
int failures=0;
#define CHECK(expr) do { if(!(expr)) { ++failures; std::cerr << "FAILED " << #expr << " line " << __LINE__ << "\n"; } } while (0)
static void invalid(const std::string& url)
{
    CHECK(!AppResourceBundle::isCanonicalUrl(url));
    AppResourceBundle bundle;
    bool failed=false;
    try { bundle.add(url,"text/html","ok"); }
    catch(const nativeweb::Error& e) {
        failed=true;
        CHECK(e.code()=="invalid_app_resource_url");
    }
    CHECK(failed);
}
int main()
{
    AppResourceBundle bundle;
    bundle.add("app://desktop/index.html","text/html",
               "<!doctype html><title>Xytron</title>");
    bundle.add("app://desktop/assets/main.js","application/javascript",
               "console.log('ready')");
    bundle.add("app://camera/ui/panel.html","text/html","camera panel");
    CHECK(bundle.size()==3u);
    AppResourceBundle snapshot(bundle);
    bundle.add("app://desktop/new.txt", "text/plain", "new");
    CHECK(snapshot.size()==3u);
    CHECK(!snapshot.contains("app://desktop/new.txt"));
    CHECK(bundle.size()==4u);
    AppResource content;
    CHECK(bundle.find("app://desktop/index.html",&content));
    CHECK(content.mimeType=="text/html");
    CHECK(content.bytes.find("Xytron")!=std::string::npos);
    CHECK(!bundle.contains("app://desktop/private.txt"));
    CHECK(!bundle.find("app://desktop/private.txt",&content));
    CHECK(!bundle.find("app://desktop/index.html",0));
    const char* malicious[] = {
        "", "http://desktop/index.html","https://desktop/index.html",
        "file:///etc/passwd","APP://desktop/index.html", "app://",
        "app://desktop","app:///index.html",
        "app://DESKTOP/index.html","app://desktop:80/index.html",
        "app://desktop//index.html", "app://desktop/../secrets",
        "app://desktop/./index.html","app://desktop/foo/../../etc/passwd",
        "app://desktop/%2e%2e/secrets","app://desktop/%2fetc/passwd",
        "app://desktop/%5cetc","app://desktop/C:\\windows",
        "app://desktop/index.html?file=/etc/passwd",
        "app://desktop/index.html#fragment",
        "app://desktop/path//nested", "app://desktop/index.html/",
        "app://desktop/@admin.html", "app://desktop/a%00b.html",
        "app://desktop/a\tb.html", "app://desktop/index.html\n"
    };
    for(size_t i=0;i<sizeof(malicious)/sizeof(malicious[0]);++i)
        invalid(malicious[i]);
    bool duplicate=false;
    try { bundle.add("app://desktop/index.html","text/html","overwrite"); }
    catch(const nativeweb::Error& e) {
        duplicate=true;
        CHECK(e.code()=="duplicate_app_resource");
    }
    CHECK(duplicate);
    bool mime=false;
    try { bundle.add("app://desktop/not.txt","text/plain; charset=utf8","bad"); }
    catch(const nativeweb::Error& e) {
        mime=true;
        CHECK(e.code()=="invalid_app_resource_mime");
    }
    CHECK(mime);
    CHECK(bundle.size()==4u);
    AppResourceBundle limited;
    bool tooLarge=false;
    try { limited.add("app://desktop/huge.bin", "application/octet-stream",
                      std::string(16u*1024u*1024u+1,'a')); }
    catch (const nativeweb::Error& e) {
        tooLarge=true;
        CHECK(e.code()=="app_resource_too_large");
    }
    CHECK(tooLarge);
    if(failures) return 1;
    std::cout<<"app:// packaged resource catalog: PASS\n";
    return 0;
}
