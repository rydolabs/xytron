#include <xytron/xytron.hpp>
#include <nativeweb/nativeweb.hpp>

#include <type_traits>

static_assert(std::is_same<xytron::WebView, nativeweb::WebView>::value,
              "Xytron WebView must keep existing Core type identity");
static_assert(std::is_same<xytron::WebViewOptions,
                           nativeweb::WebViewOptions>::value,
              "Xytron options must remain ABI-compatible");
static_assert(std::is_same<xytron::NativeObjectHandle,
                           nativeweb::NativeObjectHandle>::value,
              "Plugin object handle ABI must remain unchanged");

int main()
{
    xytron::WebView view;
    if (view.isCreated())
        return 1;

    xytron::WebViewOptions options;
    options.engine = xytron::Engine::Auto;
    if (options.engine != xytron::Engine::Auto)
        return 2;

    xytron::AppResourceBundle assets;
    if (assets.size() != 0)
        return 3;

    return 0;
}
