#include "nativeweb/plugin_abi.h"

#include <cstring>

#if defined(_WIN32)
#  define NW_TEST_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define NW_TEST_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

NW_TEST_PLUGIN_EXPORT
int32_t nativeweb_plugin_init_v1(
    nw_plugin_api_v1* plugin)
{
    if (!plugin)
        return -1;

    std::memset(
        plugin,
        0,
        sizeof(*plugin));

    plugin->struct_size =
        static_cast<uint32_t>(
            sizeof(*plugin));

    plugin->abi_version =
        NATIVEWEB_PLUGIN_ABI_V1 + 100;

    plugin->id =
        "badabi";

    plugin->version =
        "1.0";

    return 0;
}
