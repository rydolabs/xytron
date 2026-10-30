#include "nativeweb/plugin_abi.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <new>
#include <thread>

namespace {

std::atomic<int> gRegistrationCalls(0);
std::atomic<int> gRunningCalls(0);
std::atomic<int> gCompletedCalls(0);
std::atomic<int> gRunningFactories(0);
std::atomic<int> gCompletedFactories(0);

struct Counter {
    Counter() : value(42), running(false) {}
    int value;
    std::atomic<bool> running;
};

void* create(void*) { return new (std::nothrow) Counter(); }
void* createSlow(void*)
{
    gRunningFactories.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Counter* result = new (std::nothrow) Counter();
    gRunningFactories.fetch_sub(1);
    gCompletedFactories.fetch_add(1);
    return result;
}
void* createWithArgs(void*, const nw_value_v1* args, uint64_t argc,
    nw_error_v1* error)
{
    if (argc != 1 || !args || args[0].type != NW_VALUE_INT32_V1) {
        static const char code[] = "invalid_factory_arguments";
        error->code.data = code;
        error->code.size = sizeof(code) - 1;
        return 0;
    }
    Counter* result = new (std::nothrow) Counter();
    if (result) result->value = args[0].data.int32_value;
    return result;
}

void destroy(void*, void* p) { delete static_cast<Counter*>(p); }

void setInteger(nw_value_v1* result, int value)
{
    std::memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->type = NW_VALUE_INT32_V1;
    result->data.int32_value = value;
}

void setBoolean(nw_value_v1* result, bool value)
{
    std::memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->type = NW_VALUE_BOOL_V1;
    result->data.boolean_value = value ? 1 : 0;
}

int32_t add(void*, void* object, const nw_value_v1* args,
    uint64_t argc, nw_value_v1* result, nw_error_v1*)
{
    if (!object || argc != 1 || !args || args[0].type != NW_VALUE_INT32_V1)
        return -1;
    Counter* counter = static_cast<Counter*>(object);
    counter->value += args[0].data.int32_value;
    setInteger(result, counter->value);
    return 0;
}

int32_t value(void*, void* object, const nw_value_v1*,
    uint64_t argc, nw_value_v1* result, nw_error_v1*)
{
    if (!object || argc) return -1;
    setInteger(result, static_cast<Counter*>(object)->value);
    return 0;
}

int32_t running(void*, void* object, const nw_value_v1*,
    uint64_t argc, nw_value_v1* result, nw_error_v1*)
{
    if (!object || argc) return -1;
    setBoolean(result, static_cast<Counter*>(object)->running.load());
    return 0;
}

int32_t slowAdd(void* data, void* object, const nw_value_v1* args,
    uint64_t argc, nw_value_v1* result, nw_error_v1* error)
{
    Counter* counter = static_cast<Counter*>(object);
    if (!counter) return -1;
    counter->running.store(true);
    gRunningCalls.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int32_t status = add(data, object, args, argc, result, error);
    counter->running.store(false);
    gRunningCalls.fetch_sub(1);
    gCompletedCalls.fetch_add(1);
    return status;
}

int32_t fail(void*, void*, const nw_value_v1*,
    uint64_t, nw_value_v1*, nw_error_v1* error)
{
    static const char code[] = "counter_failure";
    static const char message[] = "expected object error";
    std::memset(error, 0, sizeof(*error));
    error->struct_size = sizeof(*error);
    error->code.data = code;
    error->code.size = sizeof(code) - 1;
    error->message.data = message;
    error->message.size = sizeof(message) - 1;
    return -1;
}

int32_t registerApi(void*, const nw_host_api_v1* host)
{
    gRegistrationCalls.fetch_add(1);
    if (!host || host->struct_size <
        offsetof(nw_host_api_v1, register_object_method) +
        sizeof(host->register_object_method) ||
        !host->register_object_factory || !host->register_object_method)
        return -1;

    if (host->register_object_factory(host->host_context,
        "counter", create, destroy, 0)) return -1;
    if (host->register_object_factory(host->host_context,
        "slow_counter", createSlow, destroy, 0)) return -1;

    if (host->struct_size >= offsetof(nw_host_api_v1, register_object_factory_args)
        + sizeof(host->register_object_factory_args) &&
        host->register_object_factory_args) {
        if (host->register_object_factory_args(host->host_context,
            "counter_args", createWithArgs, destroy, 0)) return -1;
    }

    const struct {
        const char* name;
        nw_plugin_object_call_v1 callback;
    } methods[] = {
        {"add", add}, {"value", value}, {"running", running},
        {"slow_add", slowAdd}, {"fail", fail}
    };
    for (std::size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        if (host->register_object_method(host->host_context,
            "counter", methods[i].name, methods[i].callback, 0)) return -1;
    }
    // The browser E2E exercises a factory-created instance with arguments.
    // Expose the same call/error methods as the legacy no-argument counter.
    if (host->register_object_method(host->host_context,
        "counter_args", "value", value, 0)) return -1;
    if (host->register_object_method(host->host_context,
        "counter_args", "add", add, 0)) return -1;
    if (host->register_object_method(host->host_context,
        "counter_args", "fail", fail, 0)) return -1;
    return 0;
}

} // namespace

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" EXPORT int32_t nativeweb_plugin_init_v1(nw_plugin_api_v1* p)
{
    if (!p) return -1;
    p->abi_version = NATIVEWEB_PLUGIN_ABI_V1;
    p->id = "objects";
    p->version = "1.0";
    p->register_api = registerApi;
    p->shutdown = 0;
    return 0;
}

extern "C" EXPORT int32_t nativeweb_test_registration_calls()
{
    return gRegistrationCalls.load();
}

extern "C" EXPORT int32_t nativeweb_test_running_calls()
{
    return gRunningCalls.load();
}

extern "C" EXPORT int32_t nativeweb_test_completed_calls()
{
    return gCompletedCalls.load();
}

extern "C" EXPORT int32_t nativeweb_test_running_factories()
{
    return gRunningFactories.load();
}

extern "C" EXPORT int32_t nativeweb_test_completed_factories()
{
    return gCompletedFactories.load();
}
