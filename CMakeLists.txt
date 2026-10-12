cmake_minimum_required(VERSION 3.20)

project(GameDev)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 加入 SDL3
add_subdirectory(vendor/SDL-main EXCLUDE_FROM_ALL)

# 加入 SDL3_image：负责把 PNG 等图片文件解码成 SDL 纹理（IMG_LoadTexture）。
# SDL3_image 默认会一并内嵌构建 AVIF/TIFF/WEBP 等格式的第三方依赖，
# 本项目目前只需要 PNG，故先关闭这三种需要额外大型依赖的格式，避免下载无用源码：
set(SDLIMAGE_AVIF OFF) # 关闭 AVIF（否则需要 libavif/dav1d/aom 三个大型依赖）
set(SDLIMAGE_TIF OFF)  # 关闭 TIFF（否则需要 libtiff 依赖）
set(SDLIMAGE_WEBP OFF) # 关闭 WebP（否则需要 libwebp 依赖）
# 关闭"依赖动态加载"模式：让 PNG 的 libpng 改为静态链入 SDL3_image.dll。
# 这样运行时只需要 SDL3.dll 和 SDL3_image.dll 两个动态库，部署更简单；
# libpng 与 zlib 源码已按官方 release-3.4.8 锁定的版本内嵌到 vendor/SDL3_image/external/。
set(SDLIMAGE_DEPS_SHARED OFF)
add_subdirectory(vendor/SDL3_image EXCLUDE_FROM_ALL)

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

# 链接 SDL3 与 SDL3_image（后者提供 IMG_LoadTexture 等图片文件加载 API）
target_link_libraries(GameDev
    PRIVATE SDL3::SDL3
    PRIVATE SDL3_image::SDL3_image
)

# 运行时把 SDL3.dll 与 SDL3_image.dll 自动复制到 GameDev.exe 所在目录
add_custom_command(TARGET GameDev POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:GameDev>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3_image::SDL3_image>
    $<TARGET_FILE_DIR:GameDev>
)