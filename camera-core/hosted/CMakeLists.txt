cmake_minimum_required(VERSION 4.0)
project(gakumas_localify LANGUAGES CXX)

set(LOCALIFY_BUILD_MODE "Hosted" CACHE STRING "Hosted or Standalone")
set_property(CACHE LOCALIFY_BUILD_MODE PROPERTY STRINGS Hosted Standalone)
if(NOT LOCALIFY_BUILD_MODE STREQUAL "Hosted" AND NOT LOCALIFY_BUILD_MODE STREQUAL "Standalone")
    message(FATAL_ERROR "LOCALIFY_BUILD_MODE must be Hosted or Standalone")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Localify requires MSVC.")
endif()

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LOCALIFY_ARTIFACT_ROOT "${ROOT}/build" CACHE PATH "Local output root; never a game directory")
option(LOCALIFY_USE_VENDORED_SDK "Build Hosted against the checked-in SDK snapshot" ON)
set(LOCALIFY_LOADER_SDK_DIR "" CACHE PATH "Optional explicit GakuLoader include directory")
if(LOCALIFY_BUILD_MODE STREQUAL "Hosted")
    if(LOCALIFY_LOADER_SDK_DIR)
        foreach(header GakuPlugin.h GakuPaths.h GakuLanguage.h GakuLifecycle.h)
            if(NOT EXISTS "${LOCALIFY_LOADER_SDK_DIR}/${header}")
                message(FATAL_ERROR "Selected Loader SDK missing ${header}")
            endif()
            file(SHA256 "${LOCALIFY_LOADER_SDK_DIR}/${header}" sdk_hash)
            file(SHA256 "${ROOT}/src/host/${header}" vendored_hash)
            if(NOT sdk_hash STREQUAL vendored_hash)
                message(FATAL_ERROR "src/host/${header} differs from the selected Loader SDK")
            endif()
        endforeach()
    elseif(LOCALIFY_USE_VENDORED_SDK)
        message(STATUS "Hosted uses the checked-in public SDK snapshot; no adjacent checkout required")
    else()
        message(FATAL_ERROR "Set LOCALIFY_LOADER_SDK_DIR or enable LOCALIFY_USE_VENDORED_SDK")
    endif()
else()
    enable_language(C ASM_MASM)
endif()

foreach(dep cpprestsdk minizip ZLIB nlohmann_json fmt)
    find_package(${dep} CONFIG REQUIRED)
endforeach()

# Conan's cpprestsdk recipe changed the exported target name; accept either so a
# dependency bump does not fail the link with an unrelated error.
if(TARGET cpprestsdk::cpprestsdk)
    set(CPPREST_TARGET cpprestsdk::cpprestsdk)
elseif(TARGET cpprestsdk::cpprest)
    set(CPPREST_TARGET cpprestsdk::cpprest)
else()
    message(FATAL_ERROR "cpprestsdk exports no known CMake target (expected cpprestsdk::cpprestsdk).")
endif()

set(LOCALIFY_SHARED_SOURCES
    "${ROOT}/src/GakumasLocalify/Hook.cpp"
    "${ROOT}/src/GakumasLocalify/PurchaseGuard.cpp"
    "${ROOT}/src/GakumasLocalify/HookTexture.cpp"
    "${ROOT}/src/GakumasLocalify/Local.cpp"
    "${ROOT}/src/GakumasLocalify/MasterLocal.cpp"
    "${ROOT}/src/GakumasLocalify/Log.cpp"
    "${ROOT}/src/GakumasLocalify/Misc.cpp"
    "${ROOT}/src/GakumasLocalify/Plugin.cpp"
    "${ROOT}/src/GakumasLocalify/GyroEmu.cpp"
    "${ROOT}/src/GakumasLocalify/camera/baseCamera.cpp"
    "${ROOT}/src/GakumasLocalify/camera/camera.cpp"
    "${ROOT}/src/GakumasLocalify/config/Config.cpp"
    "${ROOT}/src/GakumasLocalify/string_parser/StringParser.cpp"
    "${ROOT}/src/gkmsGUI/gkmsGUILoop.cpp"
    "${ROOT}/src/gkmsGUI/GUII18n.cpp"
    "${ROOT}/src/resourceUpdate/resourceUpdate.cpp"
    "${ROOT}/src/imgui/imgui.cpp"
    "${ROOT}/src/imgui/imgui_draw.cpp"
    "${ROOT}/src/imgui/imgui_tables.cpp"
    "${ROOT}/src/imgui/imgui_widgets.cpp"
)
set(HOSTED_SOURCES
    "${ROOT}/src/host/HostedPlugin.cpp"
    "${ROOT}/src/host/HostedRuntime.cpp")
set(STANDALONE_SOURCES
    "${ROOT}/src/main.cpp"
    "${ROOT}/src/windowsPlatform.cpp"
    "${ROOT}/src/console.cpp"
    "${ROOT}/src/dllproxy/proxy.cpp"
    "${ROOT}/src/dllproxy/version.asm"
    "${ROOT}/src/dllproxy/version.def"
    "${ROOT}/src/gkmsGUI/gkmsGUIMain.cpp"
    "${ROOT}/src/il2cpp/il2cpp_symbols.cpp"
    "${ROOT}/src/steam/steam.cpp"
    "${ROOT}/src/imgui/imgui_demo.cpp"
    "${ROOT}/src/imgui/imgui_impl_dx11.cpp"
    "${ROOT}/src/imgui/imgui_impl_win32.cpp")
if(LOCALIFY_BUILD_MODE STREQUAL "Hosted")
    set(LOCALIFY_TARGET GakuActors)
    set(LOCALIFY_OUTPUT_ROOT "${LOCALIFY_ARTIFACT_ROOT}/hosted")
    add_library(${LOCALIFY_TARGET} SHARED ${LOCALIFY_SHARED_SOURCES} ${HOSTED_SOURCES}
        "${ROOT}/../src/Plugin.cpp")
    target_compile_definitions(${LOCALIFY_TARGET} PRIVATE GKMS_HOSTED=1 GKMS_WINDOWS=1 GKMS_CAMERA_ONLY=1 GKMS_ACTORS_INTEGRATED=1)
    target_include_directories(${LOCALIFY_TARGET} PRIVATE "${ROOT}/../include" "${ROOT}/../src" "${ROOT}/../vendor" "${ROOT}/../vendor/imgui")
else()
    set(LOCALIFY_TARGET LocalifyStandalone)
    set(LOCALIFY_OUTPUT_ROOT "${LOCALIFY_ARTIFACT_ROOT}/standalone")
    add_library(${LOCALIFY_TARGET} SHARED ${LOCALIFY_SHARED_SOURCES} ${STANDALONE_SOURCES})
    set_target_properties(${LOCALIFY_TARGET} PROPERTIES OUTPUT_NAME "version")
    file(GLOB_RECURSE MINHOOK_SOURCES CONFIGURE_DEPENDS "${ROOT}/deps/minhook/src/*.c")
    add_library(LocalifyMinHook STATIC ${MINHOOK_SOURCES})
    target_include_directories(LocalifyMinHook PUBLIC "${ROOT}/deps/minhook/include")
    target_link_libraries(${LOCALIFY_TARGET} PRIVATE LocalifyMinHook dwmapi shell32)
endif()
target_include_directories(${LOCALIFY_TARGET} PRIVATE
    "${ROOT}/src"
    "${ROOT}/src/imgui"
    "${ROOT}/deps/rapidjson/include")
# Hosted has no MinHook include directory or link dependency.
target_compile_options(${LOCALIFY_TARGET} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/MP;/EHsc>"
    # cpprestsdk 2.10.19 forward-declares web::json::details::_Value and defines
    # it 1300 lines later, so json.h only builds while the compiler still defers
    # instantiation of unique_ptr<_Value>'s destructor. /std:c++20 implies
    # /permissive-, which instantiates it immediately and fails with
    # "can't delete an incomplete type". Localify's own code needs C++20
    # (std::ranges, string::starts_with), so conformance mode is relaxed just
    # enough for the vendored header instead of downgrading the language.
    "$<$<COMPILE_LANGUAGE:CXX>:/permissive>"
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2>")
# The shared translation code uses C++23 (string::contains, views::zip), so this
# target follows the toolchain's standard rather than pinning an older one.
set_target_properties(${LOCALIFY_TARGET} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

# cpprestsdk 2.10.19 forward-declares web::json::details::_Value and only defines
# it 1300 lines later, so cpprest/json.h builds solely while the compiler still
# defers instantiating unique_ptr<_Value>'s destructor. /std:c++latest forces
# conformance and cannot be relaxed, which breaks that header. These translation units odr-use web::json::value in a way that trips it; they
# are C++20-clean, so they are compiled in C++20 mode where /permissive still
# applies. Other TUs stay on C++23 because the shared code needs it
# (string::contains, views::zip) — if one of them ever starts failing here, that
# is a real signal, not something to paper over by widening this list.
set_source_files_properties(
    "${ROOT}/src/GakumasLocalify/Hook.cpp"
    "${ROOT}/src/GakumasLocalify/Misc.cpp"
    "${ROOT}/src/resourceUpdate/resourceUpdate.cpp"
    "${ROOT}/src/main.cpp"
    "${ROOT}/src/windowsPlatform.cpp"
    PROPERTIES COMPILE_OPTIONS "/std:c++20;/permissive")
target_link_options(${LOCALIFY_TARGET} PRIVATE /INCREMENTAL:NO)
target_link_libraries(${LOCALIFY_TARGET} PRIVATE
    ${CPPREST_TARGET} minizip::minizip ZLIB::ZLIB nlohmann_json::nlohmann_json fmt::fmt
    d3d11 d3dcompiler dxgi winhttp advapi32 user32 comdlg32 ole32 windowscodecs crypt32)
set_target_properties(${LOCALIFY_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_OUTPUT_ROOT}/$<CONFIG>"
    PDB_OUTPUT_DIRECTORY "${LOCALIFY_OUTPUT_ROOT}/$<CONFIG>")
# A crash offset from the loader's CrashTrace record is only actionable if it
# can be resolved to a function; keep the map next to the binary.
target_link_options(${LOCALIFY_TARGET} PRIVATE "/MAP:${LOCALIFY_OUTPUT_ROOT}/${LOCALIFY_TARGET}.map" "/MAPINFO:EXPORTS")

option(LOCALIFY_BUILD_UPDATE_TESTS "Build offline resource-package and recovery fixtures" OFF)
if(LOCALIFY_BUILD_UPDATE_TESTS)
    add_executable(ResourceUpdateTests "${CMAKE_CURRENT_SOURCE_DIR}/test-resource-update.cpp")
    target_include_directories(ResourceUpdateTests PRIVATE "${ROOT}/src")
    target_compile_options(ResourceUpdateTests PRIVATE /utf-8 /EHsc)
    target_compile_features(ResourceUpdateTests PRIVATE cxx_std_20)
    target_link_libraries(ResourceUpdateTests PRIVATE minizip::minizip ZLIB::ZLIB nlohmann_json::nlohmann_json advapi32)
    set_target_properties(ResourceUpdateTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/hosted/update-tests/$<CONFIG>")
    add_executable(ResourceDataLayoutTests "${CMAKE_CURRENT_SOURCE_DIR}/test-data-layout.cpp")
    target_include_directories(ResourceDataLayoutTests PRIVATE "${ROOT}/src")
    target_compile_options(ResourceDataLayoutTests PRIVATE /utf-8 /EHsc)
    target_compile_features(ResourceDataLayoutTests PRIVATE cxx_std_20)
    target_link_libraries(ResourceDataLayoutTests PRIVATE nlohmann_json::nlohmann_json advapi32)
    set_target_properties(ResourceDataLayoutTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/hosted/data-tests/$<CONFIG>")
    add_executable(ResourceLayeredUpdateTests "${CMAKE_CURRENT_SOURCE_DIR}/test-layered-update.cpp")
    target_include_directories(ResourceLayeredUpdateTests PRIVATE "${ROOT}/src")
    target_compile_options(ResourceLayeredUpdateTests PRIVATE /utf-8 /EHsc)
    target_compile_features(ResourceLayeredUpdateTests PRIVATE cxx_std_20)
    target_link_libraries(ResourceLayeredUpdateTests PRIVATE nlohmann_json::nlohmann_json advapi32)
    set_target_properties(ResourceLayeredUpdateTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/hosted/data-tests/$<CONFIG>")
    add_executable(ResourceReloadTests "${CMAKE_CURRENT_SOURCE_DIR}/test-reload-pipeline.cpp")
    target_include_directories(ResourceReloadTests PRIVATE "${ROOT}/src")
    target_compile_options(ResourceReloadTests PRIVATE /utf-8 /EHsc)
    target_compile_features(ResourceReloadTests PRIVATE cxx_std_20)
    target_link_libraries(ResourceReloadTests PRIVATE advapi32)
    set_target_properties(ResourceReloadTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/hosted/data-tests/$<CONFIG>")
    add_executable(InitialTranslationWaitTests "${CMAKE_CURRENT_SOURCE_DIR}/test-initial-translation-wait.cpp")
    target_include_directories(InitialTranslationWaitTests PRIVATE "${ROOT}/src")
    target_compile_options(InitialTranslationWaitTests PRIVATE /utf-8 /EHsc)
    target_compile_features(InitialTranslationWaitTests PRIVATE cxx_std_17)
    set_target_properties(InitialTranslationWaitTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/hosted/data-tests/$<CONFIG>")
endif()

option(LOCALIFY_BUILD_DATA_TOOLS "Build the explicit offline Localify migration tool and its fixtures" OFF)
if(LOCALIFY_BUILD_DATA_TOOLS)
    foreach(item LocalifyDataTool LocalifyDataMigrationTests)
        if(item STREQUAL "LocalifyDataTool")
            set(data_source "${CMAKE_CURRENT_SOURCE_DIR}/localify-data-tool.cpp")
        else()
            set(data_source "${CMAKE_CURRENT_SOURCE_DIR}/test-data-migration.cpp")
        endif()
        add_executable(${item} "${data_source}")
        target_include_directories(${item} PRIVATE "${ROOT}/src")
        target_compile_options(${item} PRIVATE /utf-8 /EHsc)
        target_compile_features(${item} PRIVATE cxx_std_20)
        target_link_libraries(${item} PRIVATE nlohmann_json::nlohmann_json advapi32)
        set_target_properties(${item} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/hosted/data-tools/$<CONFIG>")
    endforeach()
endif()

option(LOCALIFY_BUILD_AUTHOR_TESTS "Build production translation/configuration parser tests" OFF)
if(LOCALIFY_BUILD_AUTHOR_TESTS)
    add_executable(ProductionDataTests
        "${CMAKE_CURRENT_SOURCE_DIR}/test-production-data.cpp"
        "${ROOT}/src/GakumasLocalify/Local.cpp"
        "${ROOT}/src/GakumasLocalify/MasterLocal.cpp"
        "${ROOT}/src/GakumasLocalify/config/Config.cpp"
        "${ROOT}/src/GakumasLocalify/string_parser/StringParser.cpp"
        "${ROOT}/src/GakumasLocalify/Log.cpp"
        "${ROOT}/src/GakumasLocalify/Misc.cpp")
    target_compile_definitions(ProductionDataTests PRIVATE GKMS_HOSTED=1 GKMS_WINDOWS=1)
    target_include_directories(ProductionDataTests PRIVATE
        "${ROOT}/src" "${ROOT}/src/imgui" "${ROOT}/deps/rapidjson/include")
    target_compile_options(ProductionDataTests PRIVATE /utf-8 /EHsc /Gy)
    target_link_options(ProductionDataTests PRIVATE /OPT:REF)
    target_link_libraries(ProductionDataTests PRIVATE
        ${CPPREST_TARGET} nlohmann_json::nlohmann_json fmt::fmt advapi32)
    set_target_properties(ProductionDataTests PROPERTIES
        CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/author-tests/$<CONFIG>")
    add_executable(UpdateLifecycleTests "${CMAKE_CURRENT_SOURCE_DIR}/test-update-lifecycle.cpp")
    target_compile_definitions(UpdateLifecycleTests PRIVATE GKMS_HOSTED=1 GKMS_WINDOWS=1)
    target_include_directories(UpdateLifecycleTests PRIVATE "${ROOT}/src" "${ROOT}/deps/rapidjson/include")
    target_compile_options(UpdateLifecycleTests PRIVATE /utf-8 /EHsc /Gy /permissive)
    target_link_options(UpdateLifecycleTests PRIVATE /OPT:REF)
    target_link_libraries(UpdateLifecycleTests PRIVATE
        ${CPPREST_TARGET} minizip::minizip ZLIB::ZLIB nlohmann_json::nlohmann_json fmt::fmt advapi32)
    set_target_properties(UpdateLifecycleTests PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/author-tests/$<CONFIG>")
    add_executable(UiLanguageTests
        "${CMAKE_CURRENT_SOURCE_DIR}/test-ui-language.cpp"
        "${ROOT}/src/gkmsGUI/GUII18n.cpp")
    target_compile_options(UiLanguageTests PRIVATE /utf-8 /EHsc)
    set_target_properties(UiLanguageTests PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${LOCALIFY_ARTIFACT_ROOT}/author-tests/$<CONFIG>")
endif()
