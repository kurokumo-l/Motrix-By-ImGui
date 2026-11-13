cmake_minimum_required(VERSION 4.1)

# Must be set BEFORE project() for CMake experimental C++23 `import std;` support
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.4.0")
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "4.3.0")
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
endif()

project(motrix_native VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "Motrix Native requires the Ninja generator for C++23 import std; support.")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Third-party dependency isolation (never inherit /W4 /WX or CXX_MODULE_STD)
# ---------------------------------------------------------------------------
set(USE_ZLIB OFF CACHE BOOL "Disable zlib in IXWebSocket" FORCE)
set(USE_TLS OFF CACHE BOOL "Disable TLS in IXWebSocket" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "Disable IXWebSocket install" FORCE)

set(JSON_SystemInclude ON CACHE BOOL "Mark nlohmann_json as SYSTEM" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann_json tests" FORCE)
set(JSON_Install OFF CACHE BOOL "Disable nlohmann_json install" FORCE)

set(UPNPC_BUILD_STATIC ON CACHE BOOL "Build static miniupnpc" FORCE)
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "Disable shared miniupnpc" FORCE)
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Disable miniupnpc tests" FORCE)
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Disable miniupnpc sample" FORCE)
set(UPNPC_NO_INSTALL ON CACHE BOOL "Disable miniupnpc install" FORCE)
set(MINIUPNPC_TARGET_WINDOWS_VERSION "0x0A00" CACHE STRING "Target Windows 10+" FORCE)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)
set_target_properties(imgui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES OFF
    CXX_MODULE_STD OFF
)

add_library(stb_image_write STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image_write_impl.cpp"
)
target_include_directories(stb_image_write PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)
set_target_properties(stb_image_write PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    CXX_SCAN_FOR_MODULES OFF
    CXX_MODULE_STD OFF
)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.6
    GIT_SHALLOW TRUE
)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    miniupnpc
    GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
    GIT_TAG miniupnpc_2_3_3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR miniupnpc
)
FetchContent_MakeAvailable(nlohmann_json ixwebsocket Catch2 miniupnpc)
if(TARGET libminiupnpc-static)
    set_target_properties(libminiupnpc-static PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        CXX_MODULE_STD OFF
    )
    if(MSVC)
        target_compile_options(libminiupnpc-static PRIVATE /W0 /utf-8)
    endif()
endif()
if(TARGET ixwebsocket)
    set_target_properties(ixwebsocket PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF
        CXX_MODULE_STD OFF
    )
    if(MSVC)
        target_compile_options(ixwebsocket PRIVATE /Zc:__cplusplus /utf-8)
    endif()
endif()
if(TARGET Catch2)
    set_target_properties(Catch2 PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF
        CXX_MODULE_STD OFF
    )
    if(MSVC)
        target_compile_options(Catch2 PRIVATE /Zc:__cplusplus /utf-8)
    endif()
endif()
if(TARGET Catch2WithMain)
    set_target_properties(Catch2WithMain PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF
        CXX_MODULE_STD OFF
    )
    if(MSVC)
        target_compile_options(Catch2WithMain PRIVATE /Zc:__cplusplus /utf-8)
    endif()
endif()
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(CTest)
include(Catch)

# ---------------------------------------------------------------------------
# Strict governance helper for first-party C++23 module targets only
# ---------------------------------------------------------------------------
function(motrix_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_23)
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_MODULE_STD ON
        CXX_SCAN_FOR_MODULES ON
    )
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /WX
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor
        )
    endif()
    target_compile_definitions(${target_name} PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
    )
endfunction()

# ---------------------------------------------------------------------------
# Layer 0: motrix.core
# ---------------------------------------------------------------------------
add_library(motrix_core STATIC
    src/core/encoding.cpp
    src/core/logger.cpp
    src/core/atomic_fs.cpp
    src/core/i18n.cpp
)
target_sources(motrix_core PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES
        src/core/core.ixx
        src/core/error.ixx
        src/core/encoding.ixx
        src/core/logger.ixx
        src/core/atomic_fs.ixx
        src/core/i18n.ixx
)
target_link_libraries(motrix_core PUBLIC kernel32)
motrix_configure_target(motrix_core)

# ---------------------------------------------------------------------------
# Layer 1: motrix.domain
# ---------------------------------------------------------------------------
add_library(motrix_domain STATIC
    src/domain/task_model.cpp
    src/domain/config_schema.cpp
    src/domain/formatters.cpp
    src/domain/uri.cpp
    src/domain/tracker.cpp
    src/domain/bencode_crypto.cpp
    src/domain/bencode.cpp
    src/domain/bencode_helpers.cpp
)
target_sources(motrix_domain PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES
        src/domain/domain.ixx
        src/domain/task_model.ixx
        src/domain/config_schema.ixx
        src/domain/formatters.ixx
        src/domain/uri.ixx
        src/domain/tracker.ixx
        src/domain/bencode.ixx
)
target_link_libraries(motrix_domain
    PUBLIC
        motrix_core
    PRIVATE
        nlohmann_json::nlohmann_json
)
motrix_configure_target(motrix_domain)

# ---------------------------------------------------------------------------
# Layer 2: motrix.engine & motrix.ipc
# ---------------------------------------------------------------------------
add_library(motrix_engine STATIC
    src/engine/dialect.cpp
    src/engine/json_codec.cpp
    src/engine/json_codec_build.cpp
    src/engine/json_codec_tasks.cpp
    src/engine/ws_rpc_client.cpp
    src/engine/win32_util.cpp
    src/engine/process_manager.cpp
    src/engine/process_probe.cpp
    src/engine/tracker_winhttp.cpp
    src/engine/upnp_miniupnpc.cpp
)
target_sources(motrix_engine
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/engine/engine.ixx
            src/engine/dialect.ixx
            src/engine/rpc_client.ixx
            src/engine/process_manager.ixx
            src/engine/json_codec.ixx
            src/engine/tracker.ixx
            src/engine/upnp.ixx
    PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/engine/win32_util.ixx
)
target_link_libraries(motrix_engine
    PUBLIC
        motrix_domain
        motrix_core
    PRIVATE
        nlohmann_json::nlohmann_json
        ixwebsocket
        libminiupnpc-static
        ws2_32
        iphlpapi
        winhttp
        shell32
        ole32
)
motrix_configure_target(motrix_engine)

add_library(motrix_ipc STATIC
    src/ipc/named_pipe_ipc.cpp
)
target_sources(motrix_ipc PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES
        src/ipc/ipc.ixx
)
target_link_libraries(motrix_ipc
    PUBLIC
        motrix_core
    PRIVATE
        nlohmann_json::nlohmann_json
        kernel32
)
motrix_configure_target(motrix_ipc)

# ---------------------------------------------------------------------------
# Layer 3: motrix.platform
# ---------------------------------------------------------------------------
add_library(motrix_platform STATIC
    src/platform/win32_raii.cpp
    src/platform/win32_dx11.cpp
    src/platform/win32_window.cpp
    src/platform/win32_dialogs.cpp
    src/platform/win32_tray.cpp
    src/platform/win32_taskbar.cpp
    src/platform/win32_registry.cpp
    src/platform/win32_host.cpp
)
target_sources(motrix_platform
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/platform/platform.ixx
            src/platform/window_host.ixx
            src/platform/win32.ixx
    PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/platform/dx11_internal.ixx
            src/platform/win32_internal.ixx
)
target_link_libraries(motrix_platform
    PUBLIC
        motrix_core
    PRIVATE
        imgui
        stb_image_write
        d3d11
        dxgi
        d3dcompiler
        dwmapi
        user32
        gdi32
        shell32
        ole32
        advapi32
)
motrix_configure_target(motrix_platform)

# ---------------------------------------------------------------------------
# Layer 4: motrix.app
# ---------------------------------------------------------------------------
add_library(motrix_app STATIC
    src/app/coordinator.cpp
    src/app/coordinator_desktop.cpp
    src/app/coordinator_mock.cpp
    src/app/engine_runtime.cpp
    src/app/engine_runtime_detail.cpp
)
target_sources(motrix_app
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/app/app.ixx
            src/app/command.ixx
            src/app/snapshot.ixx
            src/app/coordinator.ixx
    PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/app/engine_runtime.ixx
)
target_link_libraries(motrix_app PUBLIC motrix_domain motrix_engine motrix_platform motrix_core)
motrix_configure_target(motrix_app)

# ---------------------------------------------------------------------------
# Layer 5: motrix.ui
# ---------------------------------------------------------------------------
add_library(motrix_ui STATIC
    src/ui/theme.cpp
    src/ui/anim.cpp
    src/ui/fonts.cpp
    src/ui/scope_guard.cpp
    src/ui/icons.cpp
    src/ui/icons_ufo.cpp
    src/ui/view_titlebar.cpp
    src/ui/view_aside.cpp
    src/ui/view_subnav.cpp
    src/ui/view_task_card.cpp
    src/ui/view_task_list.cpp
    src/ui/view_speedometer.cpp
    src/ui/view_drawer.cpp
    src/ui/view_drawer_activity.cpp
    src/ui/view_drawer_tabs.cpp
    src/ui/view_prefs.cpp
    src/ui/view_prefs_advanced.cpp
    src/ui/view_add_torrent.cpp
    src/ui/view_modals.cpp
    src/ui/shell.cpp
)
target_sources(motrix_ui
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/ui/ui.ixx
            src/ui/theme.ixx
            src/ui/anim.ixx
            src/ui/fonts.ixx
            src/ui/scope_guard.ixx
            src/ui/icons.ixx
            src/ui/shell.ixx
    PRIVATE
        FILE_SET internal_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/ui/views_internal.ixx
)
target_link_libraries(motrix_ui
    PUBLIC
        motrix_app
        motrix_domain
        motrix_platform
        motrix_core
    PRIVATE
        imgui
)
motrix_configure_target(motrix_ui)

# ---------------------------------------------------------------------------
# Executable & Tests
# ---------------------------------------------------------------------------
add_executable(motrix_native WIN32
    src/main.cpp
)
target_link_libraries(motrix_native PRIVATE
    motrix_ui
    motrix_app
    motrix_platform
    motrix_engine
    motrix_ipc
    motrix_domain
    motrix_core
)
motrix_configure_target(motrix_native)

add_custom_command(TARGET motrix_native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:motrix_native>/assets"
)

add_executable(motrix_tests
    tests/test_core.cpp
    tests/test_domain.cpp
    tests/test_bencode.cpp
    tests/test_dialect.cpp
    tests/test_engine_rpc.cpp
    tests/test_m4.cpp
)
target_link_libraries(motrix_tests PRIVATE
    Catch2::Catch2WithMain
    motrix_platform
    motrix_ipc
    motrix_engine
    motrix_domain
    motrix_core
)
motrix_configure_target(motrix_tests)
catch_discover_tests(motrix_tests)
