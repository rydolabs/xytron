# One authoritative C++11 core source list shared by the installed SDK and
# standalone CEF/WebView2 integration projects. Avoid linker regressions when
# adding Core components: never duplicate this list in another CMake project.
if(NOT DEFINED NATIVEWEB_ROOT OR NOT EXISTS "${NATIVEWEB_ROOT}/src/webview.cpp")
  message(FATAL_ERROR "NATIVEWEB_ROOT must point to the NativeWeb source tree")
endif()

set(NATIVEWEB_CORE_SOURCES)
foreach(source IN ITEMS
  src/core/app_resources.cpp
  src/core/pending_call_registry.cpp
  src/core/object_registry.cpp
  src/core/native_object_runtime.cpp
  src/core/event_dispatcher.cpp
  src/core/bridge_message.cpp
  src/core/json_codec.cpp
  src/core/bridge_runtime.cpp
  src/core/process_broker.cpp
  src/core/native_permission_policy.cpp
  src/core/module_registry.cpp
  src/core/binding_registry.cpp
  src/core/worker_pool.cpp
  src/browser/common/backend_registry.cpp
  src/plugin/shared_library.cpp
  src/plugin/plugin_module.cpp
  src/webview.cpp
)
  list(APPEND NATIVEWEB_CORE_SOURCES "${NATIVEWEB_ROOT}/${source}")
endforeach()
