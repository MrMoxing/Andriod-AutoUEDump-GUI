cmake_minimum_required(VERSION 3.10)

# 让 CMake 参数由外部(如命令行或 workflow)输入，而不是写死在这里
# 删除 NDK_PATH、CMAKE_TOOLCHAIN_FILE、CMAKE_SYSTEM_NAME、CMAKE_SYSTEM_VERSION、ANDROID_PLATFORM、ANDROID_ABI

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
project(AutoUEDump)

set(KITTYMEMORY_PATH KittyMemoryEx)
file(GLOB KITTYMEMORY_SRC ${KITTYMEMORY_PATH}/*.cpp)

set(DEPS_PATH deps)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -O0 -g -std=c++20 -fexceptions -DDEBUG -DkNO_KEYSTONE -w")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O2 -s -std=c++20 -fexceptions -DNDEBUG -DkNO_KEYSTONE -w ")

file(GLOB UE_SRC src/UE/*.cpp)
file(GLOB UE_PROFILE_SRC src/UE/UEGameProfiles/*.cpp)
file(GLOB AUTOFIX_SRC src/AutoFix/*.cpp)
file(GLOB GUI_SRC
        src/GUI/Android_draw/*.cpp
        src/GUI/Android_Graphics/*.cpp
        src/GUI/Android_my_imgui/*.cpp
        src/GUI/Android_touch/*.cpp
        src/GUI/ImGui/*.cpp
        src/GUI/ImGui/backends/*.cpp
        src/GUI/ImGui/misc/freetype/*.cpp
        src/GUI/My_Utils/*.cpp)

include_directories(
        ${DEPS_PATH}
        ${KITTYMEMORY_PATH}
        src/GUI
        src/GUI/Android_draw
        src/GUI/Android_Graphics
        src/GUI/Android_my_imgui
        src/GUI/Android_touch
        src/GUI/ImGui
        src/GUI/ImGui/backends
        src/GUI/ImGui/misc/freetype
        src/GUI/ImGui/misc/git_freetype
        src/GUI/My_Utils
        src/GUI/native_surface)

# ============ 修改点 1：删掉原来的 link_libraries，改用 find_library ============

# 查找系统库
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)
find_library(z-lib z)
find_library(dl-lib dl)

# Vulkan 只在 arm64-v8a 上可用
if(ANDROID_ABI STREQUAL "arm64-v8a")
    find_library(vulkan-lib vulkan)
    message(STATUS "Vulkan library linked for arm64-v8a")
else()
    message(STATUS "Skipping Vulkan for ${ANDROID_ABI}")
endif()

# ============ 修改点 2：动态输出目录 ============
if(DEFINED ANDROID_ABI)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/outputs/${ANDROID_ABI})
endif()

# ============ 可执行文件 ============
add_executable(AutoUEDump ${UE_SRC} ${UE_PROFILE_SRC} ${AUTOFIX_SRC} ${GUI_SRC} src/executable.cpp src/Dumper.cpp src/UPackageGenerator.cpp ${KITTYMEMORY_SRC} ${DEPS_PATH}/fmt/format.cc
        src/Utils/BufferFmt.cpp
        src/Utils/KittyCmdln.cpp
        src/Utils/ProgressUtils.cpp)

target_compile_definitions(AutoUEDump PRIVATE
        kEXECUTABLE
        VK_USE_PLATFORM_ANDROID_KHR
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES
        IMGUI_ENABLE_FREETYPE
        IMGUI_DISABLE_DEBUG_TOOLS)

# ============ 修改点 3：链接所有库 ============
target_link_libraries(AutoUEDump PRIVATE
        # 基础系统库
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${z-lib}
        ${dl-lib}
        # freetype 静态库
        ${CMAKE_SOURCE_DIR}/src/GUI/ImGui/misc/git_freetype/${ANDROID_ABI}/libfreetype.a
)

# 仅为 arm64-v8a 链接 Vulkan
if(ANDROID_ABI STREQUAL "arm64-v8a")
    target_link_libraries(AutoUEDump PRIVATE ${vulkan-lib})
endif()