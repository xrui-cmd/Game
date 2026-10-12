cmake_minimum_required(VERSION 3.20)

project(GameDev)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 加入 SDL3
add_subdirectory(vendor/SDL-main EXCLUDE_FROM_ALL)

# 创建我们的游戏程序
file(GLOB_RECURSE GAMEDEV_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/cpp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/cpp/*.c"
)

file(GLOB_RECURSE GAMEDEV_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/inc/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/inc/*.hpp"
)

add_executable(GameDev
    ${GAMEDEV_SOURCES}
    ${GAMEDEV_HEADERS}
)

# 要求 MSVC 按 UTF-8 读取源码，避免中文教学注释和错误信息受系统代码页影响
target_compile_options(GameDev PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 链接 SDL3
target_link_libraries(GameDev
    PRIVATE SDL3::SDL3
)

# 运行时把 SDL3.dll 自动复制到 GameDev.exe 所在目录
add_custom_command(TARGET GameDev POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:GameDev>
)