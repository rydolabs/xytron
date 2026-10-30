#include <nativeweb/plugin_abi.h>
#include <stddef.h>

/* Compile-only public C ABI consumer, never dependent on C++ headers. */
int nativeweb_c_abi_header_check(void)
{
    nw_host_api_v1 api = {0};
    api.struct_size = (uint32_t)sizeof(api);
    api.abi_version = NATIVEWEB_PLUGIN_ABI_V1;
    return api.register_function == NULL && api.struct_size >= sizeof(api.abi_version);
}
