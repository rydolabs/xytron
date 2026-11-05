#include "browser/common/trusted_document.hpp"
#include <iostream>
#include <string>

int main()
{
    using nativeweb::detail::trustedEntryDocument;
    const std::string app = "file:///C:/apps/xytron/ui/index.html";
    int failures = 0;
#define REQUIRE(x) do { if (!(x)) { ++failures; std::cerr << #x << " failed\n"; } } while (0)
    REQUIRE(trustedEntryDocument(app, app, true));
    REQUIRE(!trustedEntryDocument(app, app, false));
    REQUIRE(!trustedEntryDocument(app, "file:///C:/apps/other/index.html", true));
    REQUIRE(!trustedEntryDocument(app, "https://example.com", true));
    REQUIRE(!trustedEntryDocument("https://example.com", "https://example.com", true));
    REQUIRE(!trustedEntryDocument("about:blank", "about:blank", true));
    REQUIRE(!trustedEntryDocument("data:text/html,hi", "data:text/html,hi", true));
    REQUIRE(!trustedEntryDocument(app, app + "#fragment", true));
    REQUIRE(!trustedEntryDocument("file:///C:/app/index.html#fragment",
                                  "file:///C:/app/index.html#fragment", true));
    REQUIRE(!trustedEntryDocument("file:///C:/app/index.html?token=1",
                                  "file:///C:/app/index.html?token=1", true));
    REQUIRE(!trustedEntryDocument("", "", true));
    REQUIRE(trustedEntryDocument("app://desktop/index.html",
                                 "app://desktop/index.html", true));
    REQUIRE(!trustedEntryDocument("app://desktop/index.html",
                                  "app://camera/index.html", true));
    REQUIRE(!trustedEntryDocument("app://desktop/index.html",
                                  "app://desktop/index.html", false));
    REQUIRE(!trustedEntryDocument("app://desktop/../etc/passwd",
                                  "app://desktop/../etc/passwd", true));
    REQUIRE(!trustedEntryDocument("app://desktop/%2e%2e/etc/passwd",
                                  "app://desktop/%2e%2e/etc/passwd", true));
    REQUIRE(!trustedEntryDocument("https://example.com/index.html",
                                  "https://example.com/index.html", true));
    const char* ambiguous[] = {
        "file:///C:/apps/../secrets.html",
        "file:///C:/apps/./index.html",
        "file:///C:/apps//index.html",
        "file:////server/share/index.html",
        "file:///C:/apps/index.html/",
        "file:///C:/apps/%2e%2e/secrets.html",
        "file:///C:/apps/%2E./secrets.html",
        "file:///C:/apps/%2fsecrets.html",
        "file:///C:/apps/%5csecrets.html",
        "file:///C:/apps/%252e%252e/secrets.html",
        "file:///C:/apps/%00secrets.html",
        "file:///C:/apps/%zzsecrets.html",
        "file:///C:/apps/index.html\\suffix"
    };
    for (std::size_t i=0; i<sizeof(ambiguous)/sizeof(ambiguous[0]); ++i)
        REQUIRE(!trustedEntryDocument(ambiguous[i], ambiguous[i], true));
    REQUIRE(trustedEntryDocument("file:///C:/Program%20Files/xytron/index.html",
                                 "file:///C:/Program%20Files/xytron/index.html", true));
    REQUIRE(trustedEntryDocument("file:///home/user/app/index.html",
                                 "file:///home/user/app/index.html", true));
    return failures ? 1 : 0;
}
