#include <nativeweb/nativeweb.hpp>

int main()
{
    // Linking and constructing the public API must succeed with only the
    // installed package on a clean consumer build.
    nativeweb::WebView view;
    if (view.isCreated())
        return 1;

    nativeweb::WebViewOptions options;
    options.engine = nativeweb::Engine::Auto;
    return options.engine == nativeweb::Engine::Auto ? 0 : 2;
}
