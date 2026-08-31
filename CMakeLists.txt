cmake_minimum_required(VERSION 3.25)

# --- vcpkg ツールチェーンの解決 -------------------------------------------
# VCPKG_ROOT が設定されていればそれを使い、なければ C:/vcpkg にフォールバックする。
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(hm_vcpkgRoot "$ENV{VCPKG_ROOT}")
    else()
        set(hm_vcpkgRoot "C:/vcpkg")
    endif()
    set(hm_vcpkgToolchain "${hm_vcpkgRoot}/scripts/buildsystems/vcpkg.cmake")
    if(EXISTS "${hm_vcpkgToolchain}")
        set(CMAKE_TOOLCHAIN_FILE "${hm_vcpkgToolchain}" CACHE STRING "vcpkg toolchain file")
        message(STATUS "vcpkg toolchain: ${hm_vcpkgToolchain}")
    else()
        message(FATAL_ERROR "vcpkg のツールチェーンが見つかりません: ${hm_vcpkgToolchain}")
    endif()
endif()

project(heightmap_mixer VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "本プロジェクトは Windows 専用です。")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# マルチコンフィグジェネレータでも出力先を揃える。
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfgUpper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfgUpper} "${CMAKE_BINARY_DIR}/bin/${cfg}")
endforeach()

# --- Windows SDK の d3d12.lib を先に解決する -------------------------------
# Visual Studio ジェネレータでは Windows SDK の lib パスが find_library の探索対象に
# 入らないため、directx12-agility の config が失敗する。先にキャッシュへ入れておく。
if(NOT D3D12_LIB)
    if(DEFINED CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(hm_sdkVersion "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    else()
        set(hm_sdkVersion "${CMAKE_SYSTEM_VERSION}")
    endif()

    # Windows SDK のインストール先をレジストリから引く（無ければ既定パスにフォールバック）。
    cmake_host_system_information(RESULT hm_kitsRoot
        QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
        VALUE "KitsRoot10"
        VIEW 32
        ERROR_VARIABLE hm_kitsRootError)
    if(NOT hm_kitsRoot)
        set(hm_kitsRoot "C:/Program Files (x86)/Windows Kits/10")
    endif()
    file(TO_CMAKE_PATH "${hm_kitsRoot}" hm_kitsRoot)

    find_library(D3D12_LIB
        NAMES d3d12
        HINTS
            "${hm_kitsRoot}/Lib/${hm_sdkVersion}/um/x64"
            "$ENV{WindowsSdkDir}Lib/${hm_sdkVersion}/um/x64"
        PATHS ENV LIB
    )
    if(NOT D3D12_LIB)
        message(FATAL_ERROR "Windows SDK の d3d12.lib が見つかりません (SDK ${hm_sdkVersion})")
    endif()
    message(STATUS "d3d12.lib: ${D3D12_LIB}")
endif()

find_package(directx-headers CONFIG REQUIRED)
find_package(directx12-agility CONFIG REQUIRED)
find_package(winpixevent CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(D3D12MemoryAllocator CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)

set(hm_sources
    src/app/Main.cpp
    src/app/Application.cpp
    src/app/Application.h
    src/core/Log.cpp
    src/core/Log.h
    src/core/Window.cpp
    src/core/Window.h
    src/rhi/Common.cpp
    src/rhi/Common.h
    src/rhi/DeletionQueue.cpp
    src/rhi/DeletionQueue.h
    src/rhi/DescriptorHeap.cpp
    src/rhi/DescriptorHeap.h
    src/rhi/Device.cpp
    src/rhi/Device.h
    src/rhi/GpuResource.cpp
    src/rhi/GpuResource.h
    src/rhi/PipelineCache.cpp
    src/rhi/PipelineCache.h
    src/rhi/ShaderCompiler.cpp
    src/rhi/ShaderCompiler.h
    src/rhi/UploadRing.cpp
    src/rhi/UploadRing.h
    src/renderer/Camera.cpp
    src/renderer/Camera.h
    src/renderer/Mesh.cpp
    src/renderer/Mesh.h
    src/renderer/PreviewRenderer.cpp
    src/renderer/PreviewRenderer.h
    src/ui/ImGuiLayer.cpp
    src/ui/ImGuiLayer.h
)

# シェーダはビルド対象ではないが、VS のソリューションから編集できるようにしておく。
file(GLOB hm_shaders CONFIGURE_DEPENDS "shaders/*.hlsl" "shaders/*.hlsli")
set_source_files_properties(${hm_shaders} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("shaders" FILES ${hm_shaders})

add_executable(heightmap_mixer WIN32 ${hm_sources} ${hm_shaders})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${hm_sources})

target_include_directories(heightmap_mixer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(heightmap_mixer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    # PIX マーカーを有効にする。未定義だと pix3.h の呼び出しが消え、
    # Release で PIX による計測ができなくなる。
    USE_PIX
    HM_APP_VERSION="${PROJECT_VERSION}"
    # シェーダはホットリロードのためソースツリーを直接参照する。
    HM_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    $<$<CONFIG:Debug>:HM_DEBUG=1>
)

target_compile_options(heightmap_mixer PRIVATE /W4 /permissive- /utf-8 /Zc:preprocessor)

target_link_libraries(heightmap_mixer PRIVATE
    Microsoft::DirectX-Headers
    Microsoft::DirectX-Guids
    Microsoft::DirectX12-Agility
    Microsoft::WinPixEventRuntime
    Microsoft::DirectXShaderCompiler
    GPUOpen::D3D12MemoryAllocator
    imgui::imgui
    d3d12
    dxgi
    dxguid
)

# --- Agility SDK の DLL を実行ファイル横の D3D12/ に配置する ---------------
add_custom_command(TARGET heightmap_mixer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:heightmap_mixer>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PROPERTY:Microsoft::DirectX12-Core,IMPORTED_LOCATION_RELEASE>"
        "$<TARGET_FILE_DIR:heightmap_mixer>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PROPERTY:Microsoft::DirectX12-Layers,IMPORTED_LOCATION_DEBUG>"
        "$<TARGET_FILE_DIR:heightmap_mixer>/D3D12"
    COMMAND_EXPAND_LISTS
    COMMENT "Agility SDK の DLL を D3D12/ に配置"
)

# --- DXC の署名用 DLL を実行ファイル横に置く -------------------------------
# dxcompiler.dll はリンクしているため applocal で配置されるが、
# dxil.dll (署名) はリンクしていないので明示的にコピーする。
add_custom_command(TARGET heightmap_mixer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PROPERTY:Microsoft::DXIL,IMPORTED_LOCATION_DEBUG>"
        "$<TARGET_FILE_DIR:heightmap_mixer>"
    COMMAND_EXPAND_LISTS
    COMMENT "dxil.dll を配置"
)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT heightmap_mixer)
set_target_properties(heightmap_mixer PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:heightmap_mixer>"
)
