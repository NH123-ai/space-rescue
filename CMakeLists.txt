# ============================================================================
#  太空救援问题 —— CMake 工程（CLion / 命令行通用）
#----------------------------------------------------------------------------
#  目标：
#    space_rescue    正式程序：控制台报告 + trace 导出（A/B/C/D 四模块）
#    viz_test        视觉模块独立测试台（只链接 viz_module.c + 假数据）
#    viz_test_full   同上，但连真实 data_module.c / algorithm_module.c 联调
#    viz_easyx       EasyX 图形版，默认关闭（-DSPACE_RESCUE_EASYX=ON 才编译）
#
#  CLion 里打开本项目根目录即可：它会自动识别 CMakeLists.txt 并生成上面这些
#  可运行目标，无需手工建工程。
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(space_rescue LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 生成 compile_commands.json：方便 clangd / VSCode / 其它工具复用同一套编译参数
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

option(SPACE_RESCUE_EASYX "编译 EasyX 图形版（需本机已安装 EasyX）" OFF)

# 统一告警级别：与命令行手工编译的 -Wall -Wextra 保持一致
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
elseif(MSVC)
    add_compile_options(/W3)
endif()

# ---------------------------------------------------------------------------
#  视觉模块做成静态库：正式程序与测试程序共用同一份实现，
#  避免“测试通过但正式程序里是另一份代码”的错觉。
# ---------------------------------------------------------------------------
add_library(viz_module STATIC viz_module.c)
target_include_directories(viz_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(viz_module PUBLIC kernel32)
endif()

# ---------------------------------------------------------------------------
#  正式程序（组员 A + B + C + D）
# ---------------------------------------------------------------------------
add_executable(space_rescue
    main.c
    data_module.c
    algorithm_module.c
)
target_link_libraries(space_rescue PRIVATE viz_module)

# 数学库：Linux 下需要显式链接，MinGW/MSVC 下为空
if(NOT MSVC)
    target_link_libraries(space_rescue PRIVATE m)
endif()

# ---------------------------------------------------------------------------
#  视觉模块测试台（只含 viz_module.c）
# ---------------------------------------------------------------------------
add_executable(viz_test
    viz_test/viz_test.c
    viz_test/mock_data.c
)
target_include_directories(viz_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/viz_test
)
target_link_libraries(viz_test PRIVATE viz_module)

# ---------------------------------------------------------------------------
#  测试台 · 联调模式（额外链接真实的数据 / 算法模块）
#    用宏关掉 mock_data.c 里与真实模块重名的替身函数
# ---------------------------------------------------------------------------
add_executable(viz_test_full
    viz_test/viz_test.c
    viz_test/mock_data.c
    data_module.c
    algorithm_module.c
)
target_include_directories(viz_test_full PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/viz_test
)
target_compile_definitions(viz_test_full PRIVATE MOCK_WITH_REAL_MODULES)
target_link_libraries(viz_test_full PRIVATE viz_module)
if(NOT MSVC)
    target_link_libraries(viz_test_full PRIVATE m)
endif()
if(NOT MSVC)
    target_link_libraries(viz_test PRIVATE m)
endif()

# ---------------------------------------------------------------------------
#  EasyX 图形版（可选）：CLion 里勾上 SPACE_RESCUE_EASYX 才会出现该目标。
#  没装 EasyX 时不报“编译失败”，而是给出中文提示并跳过，避免误导。
# ---------------------------------------------------------------------------
if(SPACE_RESCUE_EASYX)
    include(CheckIncludeFile)
    check_include_file(graphics.h SPACE_RESCUE_HAVE_GRAPHICS_H)
    if(NOT SPACE_RESCUE_HAVE_GRAPHICS_H)
        message(WARNING
            "已请求 EasyX 版，但找不到 graphics.h（本机未安装 EasyX）。\n"
            "  已跳过 viz_easyx 目标；控制台版与网页版演示不受影响。\n"
            "  想关掉该选项：CLion → Settings → Build,Execution,Deployment → CMake\n"
            "              → CMake options 里去掉 -DSPACE_RESCUE_EASYX=ON")
    else()
        add_executable(viz_easyx
            main.c
            data_module.c
            algorithm_module.c
            viz_easyx_optional.c
        )
        target_compile_definitions(viz_easyx PRIVATE EASYX)
        target_link_libraries(viz_easyx PRIVATE viz_module)
        target_link_libraries(viz_easyx PRIVATE graphics gdi32 winmm ole32 uuid)
    endif()
endif()

message(STATUS "space_rescue: 头文件目录 = ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "space_rescue: C 标准 = C99，编译器 = ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "space_rescue: 构建类型 = ${CMAKE_BUILD_TYPE}；EasyX = ${SPACE_RESCUE_EASYX}")
