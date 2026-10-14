# ============================================================
#  AABB 碰撞单元测试 —— 独立构建工程（备选方案）
#
#  用途：当 cl 直编不可用时，改用独立 CMake 工程构建并运行测试。
#  命令（在项目根目录或任意目录执行）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build
#      tests\bin\rect_test.exe
#
#  说明：
#   - 本测试只使用 SDL3 头文件（SDL_FRect + Rect.h 内联函数），
#     不调用任何 SDL 运行时函数，因此无需链接 SDL3。
#   - 产物统一输出到 tests\bin\，避免 VS 多配置生成器的子目录歧义。
# ============================================================
cmake_minimum_required(VERSION 3.20)
project(RectCollisionTests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rect_test rect_collision_test.cpp)

if (MSVC)
    target_compile_options(rect_test PRIVATE /utf-8)
endif()

# SDL3 公共头文件（仅头文件依赖，无需链接 SDL3 库）
target_include_directories(rect_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

set_target_properties(rect_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  Config 配置解析器单元测试（config_parse_test）
#
#  与 rect_test 的差异：Config 内部使用 SDL_LoadFile 读文件、
#  SDL_LogWarn 记录警告，因此需要链接 SDL3 并携带 SDL3.dll 运行。
#
#  前置条件：主工程已构建过一次（产出 out/build/x64-debug 下的
#  SDL3.lib 导入库与 SDL3.dll）。若尚未构建请先执行：
#      cmake --preset x64-debug
#      cmake --build out/build/x64-debug
#
#  运行命令：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\config_parse_test.exe
# ============================================================
set(TESTS_SDL3_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../out/build/x64-debug")

# 主工程 SDL3 导入库不存在时给出提示（不中断配置，链接阶段仍会明确报错）
if(NOT EXISTS "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib")
    message(WARNING "未找到 ${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib，"
                    "请先构建主工程（cmake --preset x64-debug; cmake --build out/build/x64-debug）")
endif()

add_executable(config_parse_test
    config_parse_test.cpp
    ../libs/GameEngine/src/Config.cpp
)

if (MSVC)
    target_compile_options(config_parse_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Config.h 位于独立引擎仓库（libs/GameEngine/include/engine/，
# 按 "engine/Config.h" 引用），SDL 头位于 vendor/SDL-main/include
target_include_directories(config_parse_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

# 链接主工程构建出的 SDL3 导入库（提供 SDL_LoadFile / SDL_LogWarn 等符号）
target_link_libraries(config_parse_test PRIVATE
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib")

# 运行目录放一份 SDL3.dll（与主工程使用同一份构建产物，版本天然一致）
add_custom_command(TARGET config_parse_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3.dll"
    "$<TARGET_FILE_DIR:config_parse_test>"
)

# 产物统一输出到 tests\bin\，与 rect_test 一致
set_target_properties(config_parse_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  Input 输入系统单元测试（input_test）
#
#  与 config_parse_test 同模式：编译引擎源码（src/Input.cpp）并链接
#  SDL3（Input.cpp 的 update() 路径引用 SDL_GetKeyboardState）。
#  测试用 updateFromKeyArray 注入合成键盘状态，无需窗口与真实键盘，
#  边沿检测与改键行为可确定性验证。
#
#  前置条件与运行命令同 config_parse_test：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\input_test.exe
# ============================================================

add_executable(input_test
    input_test.cpp
    ../libs/GameEngine/src/Input.cpp
)

if (MSVC)
    target_compile_options(input_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Input.h 位于引擎子模块（按 "engine/Input.h" 引用），
# SDL 头位于 vendor/SDL-main/include
target_include_directories(input_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

# 链接主工程构建出的 SDL3 导入库（提供 SDL_GetKeyboardState / SDL_LogWarn 等符号）
target_link_libraries(input_test PRIVATE
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib")

# 运行目录放一份 SDL3.dll（与主工程使用同一份构建产物，版本天然一致）
add_custom_command(TARGET input_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3.dll"
    "$<TARGET_FILE_DIR:input_test>"
)

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(input_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
