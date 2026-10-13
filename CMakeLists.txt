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

# 加入 SDL3_ttf：负责 TTF/OTF 字体文件加载与文字渲染（TTF_OpenFont / TTF_RenderText_Blended）。
# 本项目的 UI 文本从 SDL 内置调试字体（仅 ASCII）迁移到它，以支持中文显示。
# SDL3_ttf 默认内嵌构建三类第三方依赖：freetype（字形光栅化，必需）、
# harfbuzz（文字整形）、plutosvg（SVG 彩色 emoji，可选）。
# 本项目 UI 文字没有彩色 emoji 需求，故关闭 plutosvg——与上面 SDL3_image
# 关闭 TIFF/WebP 同一思路：省去补取 plutosvg/plutovg 两份源码与链接开销。
set(SDLTTF_PLUTOSVG OFF)
# 兼容性处理：vendored freetype 声明 cmake_minimum_required(3.0...3.10)，
# 低于 CMake 4 可接受的下限（3.5）；本机 CMake 为 4.x，显式给出策略版本
# 下限，让 CMake 以 3.5 兼容模式配置该依赖（仅影响随后进入的子目录）
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# 依赖静态化：SDL3_ttf 在构建 vendored freetype/harfbuzz 前会先把
# BUILD_SHARED_LIBS 置 OFF（内部实现），两个依赖因此静态链入 SDL3_ttf.dll。
# 运行时只需要 SDL3.dll、SDL3_image.dll、SDL3_ttf.dll 三个动态库
add_subdirectory(vendor/SDL3_ttf EXCLUDE_FROM_ALL)

# 定向处理两个 vendored 依赖的 MSVC 编译警告，保证工程构建输出零警告：
#   - /utf-8：freetype/harfbuzz 源码里的版权注释含非 ASCII 字符，MSVC 默认
#     按系统代码页读取会报 C4819；按 UTF-8 读从根上消除（与 GameDev 目标一致）；
#   - /wd4244 /wd4267 /wd4312：上游代码既有的 size_t/__int64 到窄整型/指针的
#     转换，属上游代码遗留，与本项目无关，定向关闭（不影响其它警告）
if(MSVC)
    target_compile_options(harfbuzz PRIVATE /utf-8 /wd4244 /wd4267 /wd4312)
    target_compile_options(freetype PRIVATE /utf-8 /wd4244 /wd4267 /wd4312)
    # SDL_ttf 本体在 C 源码里内联包含 harfbuzz/harfbuzz.h（经 hb-style.h
    # 等头文件），同样按 UTF-8 读取以消除 C4819（与两个依赖一致的编码约定）
    target_compile_options(SDL3_ttf-shared PRIVATE /utf-8)
endif()

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

# 头文件搜索路径统一指向 Src/inc：engine/game 分层后，源文件里一律写
# "相对 Src/inc" 的 include（如 "engine/Rect.h"、"game/Player.h"、"GameDev.h"），
# 不再出现 "../inc/..." 这类随文件深度变化的脆弱相对路径。
# GLOB_RECURSE 本身会递归子目录，engine/game 下的源文件自动纳入构建，无需另改规则。
target_include_directories(GameDev PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src/inc)

# 要求 MSVC 按 UTF-8 读取源码，避免中文教学注释和错误信息受系统代码页影响
target_compile_options(GameDev PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 链接 SDL3、SDL3_image 与 SDL3_ttf。
# SDL3_image 提供 IMG_LoadTexture 等图片文件加载 API；
# SDL3_ttf 提供 TTF_OpenFont / TTF_RenderText_Blended 等字体与文字渲染 API
target_link_libraries(GameDev
    PRIVATE SDL3::SDL3
    PRIVATE SDL3_image::SDL3_image
    PRIVATE SDL3_ttf::SDL3_ttf
)

# 运行时把 SDL3.dll、SDL3_image.dll 与 SDL3_ttf.dll 自动复制到 GameDev.exe 所在目录
add_custom_command(TARGET GameDev POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3::SDL3>
    $<TARGET_FILE_DIR:GameDev>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3_image::SDL3_image>
    $<TARGET_FILE_DIR:GameDev>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
    $<TARGET_FILE_DIR:GameDev>
)

# 运行时把 assets/ 素材目录整体复制到 GameDev.exe 所在目录（与上面的 DLL 拷贝并列）。
# 这样 exe 以 SDL_GetBasePath() 为基准就能就近找到 assets/player.png、assets/coin.png
add_custom_command(TARGET GameDev POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:GameDev>/assets"
)

# 运行时把 config/ 配置目录复制到 GameDev.exe 所在目录（与 assets 拷贝并列）。
# 这样 exe 以 SDL_GetBasePath() 为基准读取 config/game.ini：改完配置重启即生效；
# 即使整个目录被删掉，游戏也会回退到内置默认值继续运行，不会崩溃
add_custom_command(TARGET GameDev POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "$<TARGET_FILE_DIR:GameDev>/config"
)