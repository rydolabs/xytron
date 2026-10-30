#ifndef NATIVEWEB_PLUGIN_ABI_H_INCLUDED
#define NATIVEWEB_PLUGIN_ABI_H_INCLUDED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVEWEB_PLUGIN_ABI_V1 1u
#define NATIVEWEB_PLUGIN_INIT_SYMBOL_V1 "nativeweb_plugin_init_v1"

typedef struct nw_string_view_v1
{
    const char* data;
    uint64_t size;
} nw_string_view_v1;

typedef struct nw_binary_view_v1
{
    const uint8_t* data;
    uint64_t size;
} nw_binary_view_v1;

typedef enum nw_value_type_v1
{
    NW_VALUE_NULL_V1 = 0,
    NW_VALUE_BOOL_V1 = 1,
    NW_VALUE_INT32_V1 = 2,
    NW_VALUE_DOUBLE_V1 = 3,
    NW_VALUE_STRING_V1 = 4,
    NW_VALUE_BINARY_V1 = 5
} nw_value_type_v1;

typedef struct nw_value_v1
{
    uint32_t struct_size;
    uint32_t type;

    union
    {
        uint8_t boolean_value;
        int32_t int32_value;
        double double_value;
        nw_string_view_v1 string_value;
        nw_binary_view_v1 binary_value;
    } data;
} nw_value_v1;

typedef struct nw_error_v1
{
    uint32_t struct_size;
    nw_string_view_v1 code;
    nw_string_view_v1 message;
} nw_error_v1;

/*
 * Input views are valid only for the duration of the call.
 * Output string/binary/error views must remain valid until the callback
 * returns. NativeWeb copies output data before returning to plugin code.
 */
typedef int32_t (*nw_plugin_call_v1)(
    void* user_data,
    const nw_value_v1* args,
    uint64_t argc,
    nw_value_v1* result,
    nw_error_v1* error);

typedef int32_t (*nw_host_register_function_v1)(
    void* host_context,
    const char* relative_name,
    nw_plugin_call_v1 function,
    void* user_data);

/* Object factories are an optional, size-gated extension to ABI v1.
 * Returned objects must be destroyed by the providing module. */
typedef void* (*nw_plugin_create_object_v1)(void* user_data);
typedef void (*nw_plugin_destroy_object_v1)(void* user_data, void* object);
typedef int32_t (*nw_host_register_object_factory_v1)(
    void* host_context,
    const char* relative_type_name,
    nw_plugin_create_object_v1 create,
    nw_plugin_destroy_object_v1 destroy,
    void* user_data);

/* Optional ABI v1 extension: instance methods receive the plugin-owned
 * instance, never an STL/C++ object across the binary boundary. */
typedef int32_t (*nw_plugin_object_call_v1)(
    void* user_data,
    void* object,
    const nw_value_v1* args,
    uint64_t argc,
    nw_value_v1* result,
    nw_error_v1* error);
typedef int32_t (*nw_host_register_object_method_v1)(
    void* host_context,
    const char* relative_type_name,
    const char* relative_method_name,
    nw_plugin_object_call_v1 function,
    void* user_data);

/* Optional appended ABI v1 extension for factories with arguments.
 * The caller owns args; they are valid only during the callback. */
typedef void* (*nw_plugin_create_object_args_v1)(
    void* user_data, const nw_value_v1* args, uint64_t argc,
    nw_error_v1* error);
typedef int32_t (*nw_host_register_object_factory_args_v1)(
    void* host_context, const char* relative_type_name,
    nw_plugin_create_object_args_v1 create,
    nw_plugin_destroy_object_v1 destroy, void* user_data);

/* ABI v1 additive extension. Types use NW_VALUE_*; result NULL denotes void.
 * Metadata pointers are valid during registration only and are copied by host.
 * Type metadata supports scalar/binary values, not objects or collections.
 */
typedef struct nw_function_signature_v1
{
    uint32_t struct_size;
    uint32_t result_type;
    const uint32_t* argument_types;
    uint32_t argument_count;
} nw_function_signature_v1;

typedef int32_t (*nw_host_register_typed_function_v1)(
    void* host_context,
    const char* relative_name,
    nw_plugin_call_v1 function,
    void* user_data,
    const nw_function_signature_v1* signature);

/* Optional, size-gated ABI v1 extension: a plugin can publish a native
 * event during a host-initiated function call. The event is namespaced as
 * <plugin_id>.<relative_event>. The host immediately copies the payload.
 *
 * Synchronous-only lifetime rule: call only while a registered plugin
 * function is executing; never retain/use host_context from background
 * threads or after plugin unload. This function is not an asynchronous
 * task scheduler. Subscribers are bound to their JS document/session. */
typedef int32_t (*nw_host_publish_event_v1)(
    void* host_context, const char* relative_event,
    const nw_value_v1* payload);

typedef struct nw_host_api_v1
{
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    nw_host_register_function_v1 register_function;
    /* Check struct_size before accessing this optional appended field. */
    nw_host_register_object_factory_v1 register_object_factory;
    /* Check struct_size before accessing this optional appended field. */
    nw_host_register_object_method_v1 register_object_method;
    /* Check struct_size before using this optional appended field. */
    nw_host_register_object_factory_args_v1 register_object_factory_args;
    /* Check struct_size before accessing the optional typed registration. */
    nw_host_register_typed_function_v1 register_typed_function;
    /* Optional ABI v1; guard by struct_size. */
    nw_host_publish_event_v1 publish_event;
} nw_host_api_v1;

typedef int32_t (*nw_plugin_register_api_v1)(
    void* plugin_context,
    const nw_host_api_v1* host);

typedef void (*nw_plugin_shutdown_v1)(
    void* plugin_context);

typedef struct nw_plugin_api_v1
{
    uint32_t struct_size;
    uint32_t abi_version;

    const char* id;
    const char* version;

    void* plugin_context;
    nw_plugin_register_api_v1 register_api;
    nw_plugin_shutdown_v1 shutdown;
} nw_plugin_api_v1;

/*
 * The single well-known exported symbol for ABI v1.
 * The plugin fills nw_plugin_api_v1; the host validates it before invoking
 * register_api.
 */
typedef int32_t (*nw_plugin_init_v1)(
    nw_plugin_api_v1* plugin);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
