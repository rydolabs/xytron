if(NOT DEFINED XYTRON OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "XYTRON and TEST_ROOT required")
endif()
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
function(ok)
  execute_process(COMMAND "${XYTRON}" ${ARGN}
    WORKING_DIRECTORY "${TEST_ROOT}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Failed: ${ARGN}, rc=${rc}, ${out}, ${err}")
  endif()
endfunction()
function(bad code)
  execute_process(COMMAND "${XYTRON}" ${ARGN}
    WORKING_DIRECTORY "${TEST_ROOT}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL code)
    message(FATAL_ERROR "Expected error ${code}, got ${rc}: ${ARGN}")
  endif()
endfunction()
ok(--help)
ok(--version)
ok(new desktop cpp_app --backend cpp)
ok(new desktop ai_app --backend python)
ok(new desktop erp_app --backend csharp)
ok(plugin new camera --language cpp)
ok(new plugin vision --lang cpp)
ok(new plugin analytics --lang csharp)
ok(new plugin automation --lang python)
ok(new plugin defaults)
ok(new app workstation)
ok(new app data_workbench --backend python)
foreach(p cpp_app/backend/main.cpp ai_app/backend/main.py
          erp_app/backend/Program.cs erp_app/backend/MainBackend.csproj
          plugins/camera/plugin.json plugins/camera/plugin.cpp
          plugins/camera/CMakeLists.txt
          plugins/vision/plugin.cpp
          plugins/analytics/Plugin.csproj
          plugins/analytics/src/Plugin.cs
          plugins/analytics/src/ExportAttribute.cs
          plugins/automation/pyproject.toml
          plugins/automation/src/automation/plugin.py
          plugins/defaults/plugin.cpp
          workstation/backend/main.cpp)
  if(NOT EXISTS "${TEST_ROOT}/${p}")
    message(FATAL_ERROR "Missing generated file: ${p}")
  endif()
endforeach()
file(READ "${TEST_ROOT}/ai_app/xytron.json" manifest)
if(NOT manifest MATCHES "\"ui\": \\{\"entry\":\"app://desktop/index.html\"")
  message(FATAL_ERROR "Default app:// desktop UI manifest missing")
endif()
if(NOT manifest MATCHES "\"language\": \"python\"")
  message(FATAL_ERROR "Main backend manifest missing: ${manifest}")
endif()
file(READ "${TEST_ROOT}/plugins/camera/plugin.json" m)
if(NOT m MATCHES "\"runtime\": \"native\"")
  message(FATAL_ERROR "Plugin subtype missing: ${m}")
endif()
bad(1 new desktop cpp_app --backend cpp)
bad(1 new desktop ../../escape --backend cpp)
bad(1 new desktop wrong --backend rust)
bad(1 plugin new ../../escape --language cpp)
bad(1 plugin new camera --language python)
bad(1 new plugin invalid --lang rust)
bad(1 new plugin ../../escape --lang python)
bad(1 new plugin analytics --lang csharp)
bad(1 new app workstation)
bad(2 run)
bad(1 publish)
bad(1 pack)
# A package must not be emitted for a C++ plugin that has not compiled.
execute_process(COMMAND "${XYTRON}" plugin pack vision
  WORKING_DIRECTORY "${TEST_ROOT}" RESULT_VARIABLE unbuilt)
if(NOT unbuilt EQUAL 1)
  message(FATAL_ERROR "Unbuilt native plugin was packaged")
endif()
bad(1 doctor --sdk invalid)
# No implicit installation: a build attempt without SDK has to fail;
# a malicious SDK prefix must be rejected before executing a shell.
bad(1 plugin build camera --sdk "invalid&echo")
bad(1 plugin build ../../escape --sdk missing)
# A single Python Main may have C++, Python and C# feature modules.
function(in_project project)
  execute_process(COMMAND "${XYTRON}" ${ARGN}
    WORKING_DIRECTORY "${TEST_ROOT}/${project}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Module command failed ${ARGN} rc=${rc}: ${out} ${err}")
  endif()
endfunction()
in_project(ai_app module add camera --language cpp)
in_project(ai_app module add agent --language python)
in_project(ai_app module add erp --language csharp)
# End-to-end CLI hard-link denial: module add must not truncate an aliased
# project manifest or mutate the linked victim outside the project directory.
file(READ "${TEST_ROOT}/ai_app/xytron.json" original_manifest)
file(CREATE_LINK "${TEST_ROOT}/ai_app/xytron.json"
     "${TEST_ROOT}/aliased-manifest.json" RESULT link_result)
if(NOT link_result STREQUAL "0")
  message(FATAL_ERROR "Cannot establish hard-link regression fixture: ${link_result}")
endif()
execute_process(COMMAND "${XYTRON}" module add hardprobe --language python
  WORKING_DIRECTORY "${TEST_ROOT}/ai_app"
  RESULT_VARIABLE hardlink_denied OUTPUT_VARIABLE hardlink_out ERROR_VARIABLE hardlink_err)
if(NOT hardlink_denied EQUAL 1)
  message(FATAL_ERROR "module add accepted hard-linked manifest: ${hardlink_out} ${hardlink_err}")
endif()
file(READ "${TEST_ROOT}/aliased-manifest.json" linked_after)
if(NOT linked_after STREQUAL original_manifest)
  message(FATAL_ERROR "CLI modified hard-linked manifest victim")
endif()
if(EXISTS "${TEST_ROOT}/ai_app/modules/hardprobe")
  message(FATAL_ERROR "CLI created a module before rejecting hard-linked manifest")
endif()
file(REMOVE "${TEST_ROOT}/aliased-manifest.json")

foreach(p modules/camera/module.cpp
          modules/agent/module.py
          modules/erp/Module.csproj)
  if(NOT EXISTS "${TEST_ROOT}/ai_app/${p}")
    message(FATAL_ERROR "Missing generated Module: ${p}")
  endif()
endforeach()
file(READ "${TEST_ROOT}/ai_app/xytron.json" expanded)
foreach(name camera agent erp)
  if(NOT expanded MATCHES "\"id\":\"${name}\"")
    message(FATAL_ERROR "Missing Module ${name} in manifest")
  endif()
endforeach()
execute_process(COMMAND "${XYTRON}" module add camera --language cpp
  WORKING_DIRECTORY "${TEST_ROOT}/ai_app" RESULT_VARIABLE duplicate)
if(NOT duplicate EQUAL 1)
  message(FATAL_ERROR "Duplicate Module id was accepted")
endif()
execute_process(COMMAND "${XYTRON}" module add ../../bad --language python
  WORKING_DIRECTORY "${TEST_ROOT}/ai_app" RESULT_VARIABLE traversal)
if(NOT traversal EQUAL 1)
  message(FATAL_ERROR "Path traversal was accepted")
endif()
bad(1 module add erp --language csharp)

foreach(runtime "native" "dotnet" "python")
  if(runtime STREQUAL "native")
    set(target "vision")
  elseif(runtime STREQUAL "dotnet")
    set(target "analytics")
  else()
    set(target "automation")
  endif()
  file(READ "${TEST_ROOT}/plugins/${target}/plugin.json" doc)
  string(JSON actual ERROR_VARIABLE parse_err GET "${doc}" runtime)
  if(parse_err OR NOT actual STREQUAL runtime)
    message(FATAL_ERROR "Plugin runtime mismatch: ${target}: ${doc}")
  endif()
  string(JSON id GET "${doc}" name)
  if(NOT id STREQUAL target)
    message(FATAL_ERROR "Plugin name mismatch: ${target}")
  endif()
endforeach()
file(READ "${TEST_ROOT}/workstation/xytron.json" app)
string(JSON backend GET "${app}" main language)
if(NOT backend STREQUAL "cpp")
  message(FATAL_ERROR "Default backend is not cpp")
endif()
file(READ "${TEST_ROOT}/plugins/analytics/Plugin.csproj" csproj)
if(NOT csproj MATCHES "net8.0")
  message(FATAL_ERROR "Generated .NET plugin needs target framework")
endif()
# Package Python source with a manifest; ZIP structure is separately
# verified by Python's standard zipfile module in the integration test.
execute_process(COMMAND "${XYTRON}" pack
  WORKING_DIRECTORY "${TEST_ROOT}/plugins/automation"
  RESULT_VARIABLE python_pack_rc)
if(NOT python_pack_rc EQUAL 0 OR
   NOT EXISTS "${TEST_ROOT}/plugins/automation/dist/automation-1.0.0-any.xplugin")
  message(FATAL_ERROR "Python plugin packaging failed")
endif()
execute_process(COMMAND "${XYTRON}" pack
  WORKING_DIRECTORY "${TEST_ROOT}/plugins/automation"
  RESULT_VARIABLE duplicate_pack_rc)
if(NOT duplicate_pack_rc EQUAL 1)
  message(FATAL_ERROR "Existing package overwritten")
endif()
execute_process(COMMAND "${XYTRON}" publish
  WORKING_DIRECTORY "${TEST_ROOT}/plugins/automation"
  RESULT_VARIABLE published)
if(NOT published EQUAL 0 OR
   NOT EXISTS "${TEST_ROOT}/plugins/automation/releases/automation-1.0.0-any.xplugin")
  message(FATAL_ERROR "Local publish failed")
endif()
execute_process(COMMAND "${XYTRON}" publish
  WORKING_DIRECTORY "${TEST_ROOT}/plugins/automation"
  RESULT_VARIABLE duplicate_publish)
if(NOT duplicate_publish EQUAL 1)
  message(FATAL_ERROR "Published package overwritten")
endif()
message(STATUS "Xytron bootstrap CLI scaffold PASS")
