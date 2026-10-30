#include "nativeweb/plugin_abi.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

std::atomic<bool> gSlowRunning(false);

int32_t addCall(
    void*,
    const nw_value_v1* args,
    uint64_t argc,
    nw_value_v1* result,
    nw_error_v1*)
{
    if (
        argc != 2 ||
        !args ||
        args[0].type != NW_VALUE_INT32_V1 ||
        args[1].type != NW_VALUE_INT32_V1)
    {
        return -1;
    }

    result->struct_size =
        static_cast<uint32_t>(
            sizeof(*result));

    result->type =
        NW_VALUE_INT32_V1;

    result->data.int32_value =
        args[0].data.int32_value +
        args[1].data.int32_value;

    return 0;
}

int32_t failCall(
    void*,
    const nw_value_v1*,
    uint64_t,
    nw_value_v1*,
    nw_error_v1* error)
{
    static const char code[] =
        "sample_error";

    static const char message[] =
        "expected plugin failure";

    error->struct_size =
        static_cast<uint32_t>(
            sizeof(*error));

    error->code.data = code;
    error->code.size =
        sizeof(code) - 1;

    error->message.data =
        message;

    error->message.size =
        sizeof(message) - 1;

    return -1;
}

int32_t slowAddCall(
    void*,
    const nw_value_v1* args,
    uint64_t argc,
    nw_value_v1* result,
    nw_error_v1*)
{
    if (
        argc != 2 ||
        !args ||
        args[0].type != NW_VALUE_INT32_V1 ||
        args[1].type != NW_VALUE_INT32_V1)
    {
        return -1;
    }

    gSlowRunning.store(true);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(250));

    result->struct_size =
        static_cast<uint32_t>(
            sizeof(*result));

    result->type =
        NW_VALUE_INT32_V1;

    result->data.int32_value =
        args[0].data.int32_value +
        args[1].data.int32_value;

    gSlowRunning.store(false);
    return 0;
}

int32_t isSlowRunningCall(
    void*,
    const nw_value_v1*,
    uint64_t argc,
    nw_value_v1* result,
    nw_error_v1*)
{
    if (argc != 0)
        return -1;

    result->struct_size =
        static_cast<uint32_t>(
            sizeof(*result));

    result->type =
        NW_VALUE_BOOL_V1;

    result->data.boolean_value =
        gSlowRunning.load()
            ? 1u
            : 0u;

    return 0;
}

int32_t registerApi(
    void*,
    const nw_host_api_v1* host)
{
    if (
        !host ||
        host->abi_version !=
            NATIVEWEB_PLUGIN_ABI_V1 ||
        !host->register_function)
    {
        return -1;
    }

    if (
        host->register_function(
            host->host_context,
            "add",
            &addCall,
            0) != 0)
    {
        return -1;
    }

    if (
        host->register_function(
            host->host_context,
            "fail",
            &failCall,
            0) != 0)
    {
        return -1;
    }

    if (
        host->register_function(
            host->host_context,
            "slow_add",
            &slowAddCall,
            0) != 0)
    {
        return -1;
    }

    if (
        host->register_function(
            host->host_context,
            "is_slow_running",
            &isSlowRunningCall,
            0) != 0)
    {
        return -1;
    }

    return 0;
}

} // namespace

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
        NATIVEWEB_PLUGIN_ABI_V1;

    plugin->id =
        "sample";

    plugin->version =
        "1.0.0";

    plugin->register_api =
        &registerApi;

    return 0;
}
