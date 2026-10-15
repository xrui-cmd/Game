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
# 加上 SDL3_mixer（见下），运行时一共需要四个动态库：
# SDL3.dll、SDL3_image.dll、SDL3_ttf.dll、SDL3_mixer.dll
add_subdirectory(vendor/SDL3_ttf EXCLUDE_FROM_ALL)

# 加入 SDL3_mixer：负责音效文件的加载与播放
# （MIX_Init / MIX_CreateMixerDevice / MIX_LoadAudio / MIX_PlayTrack）。
# 本项目只需要 WAV——它是 SDL_mixer 自带的内置解码器（src/decoder_wav.c），
# 不依赖任何第三方源码（明确不需要从 external/ 补取任何子模块）。
# 因此把其余编解码器全部显式关闭——与上面 SDL3_image 只开 PNG、SDL3_ttf
# 关闭 plutosvg 同一思路：不牵入用不到的大型依赖：
#   - OGG/FLAC 需要同时补取 libogg/libflac；Opus 还要 opus+opusfile；
#   - MOD 需要 libxmp；MIDI 会牵入 timidity（FluidSynth 需系统库）；
#   - MP3 有 dr_mp3 内嵌实现，但本项目没有 MP3 素材，同样关掉省开销。
# 关闭后真正参与编译的只有 WAV，以及始终内置的 RAW/SINEWAVE
# 两个「内存音源」解码器（源码无第三方依赖）；其余 decoder_*.c 因缺少
# 对应的 DECODER_* 宏而整体编译为空，不会引入任何外部头文件
set(SDLMIXER_AIFF OFF)              # 关闭 AIFF（内置实现，用不到）
set(SDLMIXER_VOC OFF)               # 关闭 VOC（内置实现，用不到）
set(SDLMIXER_AU OFF)                # 关闭 AU（内置实现，用不到）
set(SDLMIXER_FLAC OFF)              # 关闭 FLAC（否则补取 libflac/libogg 源码）
set(SDLMIXER_GME OFF)               # 关闭 GME 游戏音乐格式（否则补取 libgme）
set(SDLMIXER_MOD OFF)               # 关闭 MOD 模块音乐（否则补取 libxmp）
set(SDLMIXER_MP3 OFF)               # 关闭 MP3（dr_mp3/mpg123 均不需要）
set(SDLMIXER_MIDI OFF)              # 关闭 MIDI（避免 timidity/FluidSynth）
set(SDLMIXER_OPUS OFF)              # 关闭 Opus（否则补取 opus + opusfile）
set(SDLMIXER_VORBIS_STB OFF)        # 关闭 stb_vorbis 版 Ogg Vorbis
set(SDLMIXER_VORBIS_VORBISFILE OFF) # 关闭 libvorbisfile 版 Ogg Vorbis
set(SDLMIXER_WAVPACK OFF)           # 关闭 WavPack（否则补取 wavpack 源码）
# 唯一保留的编解码器：WAVE（默认即 ON，这里显式写出，表明是刻意选择而非默认残留）
set(SDLMIXER_WAVE ON)
add_subdirectory(vendor/SDL3_mixer EXCLUDE_FROM_ALL)

# 定向处理 vendored 依赖的 MSVC 编译警告，保证工程构建输出零警告：
#   - /utf-8：freetype/harfbuzz/SDL3_mixer 源码里的注释含非 ASCII 字符，
#     MSVC 默认按系统代码页读取会报 C4819；按 UTF-8 读从根上消除（与 GameDev 一致）；
#   - /wd4244 /wd4267 /wd4312：上游代码既有的 size_t/__int64 到窄整型/指针的
#     转换，属上游代码遗留，与本项目无关，定向关闭（不影响其它警告）
if(MSVC)
    target_compile_options(harfbuzz PRIVATE /utf-8 /wd4244 /wd4267 /wd4312)
    target_compile_options(freetype PRIVATE /utf-8 /wd4244 /wd4267 /wd4312)
    # SDL_ttf 本体在 C 源码里内联包含 harfbuzz/harfbuzz.h（经 hb-style.h
    # 等头文件），同样按 UTF-8 读取以消除 C4819（与两个依赖一致的编码约定）
    target_compile_options(SDL3_ttf-shared PRIVATE /utf-8)
    # SDL3_mixer 的源文件注释同样含非 ASCII 字符（如 decoder_aiff.c /
    # decoder_au.c 的版权头）；即使这两个解码器已被关闭（编译为空壳），
    # MSVC 预处理时仍会读取整个文件、照样报 C4819——同样从根上消除
    target_compile_options(SDL3_mixer-shared PRIVATE /utf-8)
endif()

# 加入独立引擎仓库（libs/GameEngine 子模块）：引擎层（Rect/Config/Entity/Font/
# SceneManager/AudioSystem 等）已从本仓库抽离为独立 git 仓库，经 git 子模块接入。
# include 约定不变："engine/xxx.h" 由 GameEngine target 以 PUBLIC include 目录
# （libs/GameEngine/include）导出，游戏侧业务代码零改动。
# 未初始化子模块时给出明确指引；引擎自身的 CMake 会在缺少 SDL target 时报错提示
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/GameEngine/CMakeLists.txt")
    message(FATAL_ERROR "引擎子模块未就绪：请先执行 git submodule update --init --recursive")
endif()
add_subdirectory(libs/GameEngine)

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

# 头文件搜索路径统一指向 Src/inc：源文件里一律写「相对 include 根」的 include
# （如 "GameDev.h"、"game/Player.h"），不出现 "../inc/..." 这类随文件深度变化的脆弱相对路径。
# engine/ 层头文件已随独立引擎仓库移出本目录：由链接的 GameEngine target 以 PUBLIC
# include 目录（libs/GameEngine/include）导出，"engine/Rect.h" 等写法保持不变，
# 游戏侧业务代码零改动。GLOB_RECURSE 本身会递归子目录，game/ 下的源文件自动纳入构建。
target_include_directories(GameDev PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Src/inc)

# 要求 MSVC 按 UTF-8 读取源码，避免中文教学注释和错误信息受系统代码页影响
target_compile_options(GameDev PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 链接 SDL3、SDL3_image、SDL3_ttf、SDL3_mixer 与独立引擎库 GameEngine。
# SDL3_image 提供 IMG_LoadTexture 等图片文件加载 API；
# SDL3_ttf 提供 TTF_OpenFont / TTF_RenderText_Blended 等字体与文字渲染 API；
# SDL3_mixer 提供 MIX_Init / MIX_LoadAudio / MIX_PlayTrack 等音效加载与播放 API。
# 四个 SDL 组件保持直接链接：外壳（GameDev.cpp）仍直接调用 TTF_Init 等 API，
# 它们的链接关系不因引擎抽取而变化；GameEngine 提供引擎层实现
# （Config/Entity/Font/SceneManager/AudioSystem 等），以 PUBLIC include 传递
# "engine/xxx.h" 头目录，静态库在链接期汇入 GameDev.exe
target_link_libraries(GameDev
    PRIVATE SDL3::SDL3
    PRIVATE SDL3_image::SDL3_image
    PRIVATE SDL3_ttf::SDL3_ttf
    PRIVATE SDL3_mixer::SDL3_mixer
    PRIVATE GameEngine
)

# 运行时把 SDL3.dll、SDL3_image.dll、SDL3_ttf.dll 与 SDL3_mixer.dll
# 自动复制到 GameDev.exe 所在目录
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
    $<TARGET_FILE_DIR:GameDev>
)

# 运行时把 assets/ 素材目录整体复制到 GameDev.exe 所在目录（与上面的 DLL 拷贝并列）。
# 这样 exe 以 SDL_GetBasePath() 为基准就能就近找到 assets/player_sheet.png、
# assets/monster_*_sheet.png 等素材（素材由 tools/compose_sprites.ps1 从原始帧组装）
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