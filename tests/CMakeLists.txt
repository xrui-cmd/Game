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
