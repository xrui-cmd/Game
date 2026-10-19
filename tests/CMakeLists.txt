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

# ============================================================
#  XP 升级曲线单元测试（xp_test）
#
#  与前三者的差异：被测对象是 game 层的纯逻辑头文件
#  （Src/inc/game/LevelCurve.h），不依赖 SDL3、无 .cpp、无链接——
#  因此本目标只需头文件搜索路径，任何环境下都可确定性构建运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\xp_test.exe
# ============================================================

add_executable(xp_test xp_test.cpp)

if (MSVC)
    target_compile_options(xp_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：LevelCurve.h 位于游戏仓 Src/inc（按 "game/LevelCurve.h" 引用）
target_include_directories(xp_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(xp_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  横扫弧形判定单元测试（attack_arc_test）
#
#  与 xp_test 同模式：被测对象是 game 层的纯逻辑头文件
#  （Src/inc/game/AttackArc.h），只用引擎 math 层的 Vec2 与标准库，
#  不依赖 SDL3、无 .cpp、无链接。include 需要两个目录：
#    - Src/inc               （"game/AttackArc.h"）
#    - libs/GameEngine/include（"engine/math/Vec2.h"）
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\attack_arc_test.exe
# ============================================================

add_executable(attack_arc_test attack_arc_test.cpp)

if (MSVC)
    target_compile_options(attack_arc_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：AttackArc.h 位于游戏仓 Src/inc，Vec2.h 位于引擎子模块
target_include_directories(attack_arc_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(attack_arc_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  世界与相机单元测试（world_test，批二）
#
#  被测对象：game/World.h + World.cpp（世界生成 / 格级碰撞 / 激活窗口）
#  以及引擎的 Camera.h（header-only：坐标换算 / 边界钳制 / 平滑跟随）。
#
#  与 config_parse_test / input_test 同模式：编译游戏源码（Src/cpp/game/
#  World.cpp）并链接 SDL3 与 SDL3_image（World.cpp 的纹理路径引用
#  IMG_LoadTexture 等符号）。测试只走纯计算路径（生成/查询/滑动），
#  不创建窗口、不加载素材，任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\world_test.exe
# ============================================================

add_executable(world_test
    world_test.cpp
    ../Src/cpp/game/World.cpp
)

if (MSVC)
    target_compile_options(world_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：World.h 位于游戏仓 Src/inc（"game/World.h"）、
# Camera.h 位于引擎子模块（"engine/Camera.h"）、SDL 头位于 vendor
target_include_directories(world_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3_image/include")

# 链接主工程构建出的 SDL3 与 SDL3_image 导入库
#（World.cpp 引用 SDL_LogInfo 与 IMG_LoadTexture 两组符号）
target_link_libraries(world_test PRIVATE
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib"
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL3_image/SDL3_image.lib")

# 运行目录放 SDL3.dll 与 SDL3_image.dll（与主工程同一份构建产物）
add_custom_command(TARGET world_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3.dll"
    "$<TARGET_FILE_DIR:world_test>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3_image.dll"
    "$<TARGET_FILE_DIR:world_test>"
)

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(world_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  三选一升级卡单元测试（card_test，M2）
#
#  被测对象：Src/inc/game/UpgradeCard.h（game 层纯逻辑头文件，header-only）——
#  九卡表 / 三抽不重复（排除满层、不足三张按实际）/ 种子化随机源 /
#  叠加数学（冷却乘算、加成累加、上限封顶）/ 升级欠账队列。
#
#  与 xp_test / monster_ai_test 同模式：不依赖 SDL3、无 .cpp、无链接，
#  随机源经注入固定序列/种子，任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\card_test.exe
# ============================================================

add_executable(card_test card_test.cpp)

if (MSVC)
    target_compile_options(card_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：UpgradeCard.h 位于游戏仓 Src/inc（按 "game/UpgradeCard.h" 引用）；
# 批五A 起增加 vendor 的 SDL3 头目录——CardLayout.h（面板布局纯函数）
# 需要 SDL_rect.h 的 SDL_FRect / SDL_PointInRectFloat（仅类型与内联函数，
# 不链接 SDL3 运行库）
target_include_directories(card_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(card_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  怪物 AI 状态决策单元测试（monster_ai_test，批三）
#
#  被测对象：Src/inc/game/MonsterAi.h（game 层纯逻辑头文件，header-only）——
#  巡逻/仇恨/栓绳回巢状态机与回巢回血纯函数。
#
#  与 xp_test / attack_arc_test 同模式：不依赖 SDL3、无 .cpp、无链接，
#  随机源经 setRandom 注入固定序列，任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\monster_ai_test.exe
# ============================================================

add_executable(monster_ai_test monster_ai_test.cpp)

if (MSVC)
    target_compile_options(monster_ai_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：MonsterAi.h 位于游戏仓 Src/inc（按 "game/MonsterAi.h" 引用）
target_include_directories(monster_ai_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(monster_ai_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  碰撞盒 / 最小分离 / 追踪吸附单元测试（collision_homing_test，批四）
#
#  被测对象：引擎 Entity 碰撞盒（libs/GameEngine 的 Entity.h/.cpp）+
#  game/Separation.h（最小分离 MTV）+ game/Homing.h（追踪吸附单步积分）。
#
#  与 world_test 同模式：编译 Entity.cpp 并链接 SDL3 与 SDL3_image
#  （Entity.cpp 的加载路径引用 IMG_LoadTexture 等符号）。实际跑到的
#  路径全部是纯计算（碰撞盒换算 / 分离向量 / 追踪位移），不创建窗口、
#  不加载素材，任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\collision_homing_test.exe
# ============================================================

add_executable(collision_homing_test
    collision_homing_test.cpp
    ../libs/GameEngine/src/Entity.cpp
)

if (MSVC)
    target_compile_options(collision_homing_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Separation.h / Homing.h 在游戏仓 Src/inc，
# Entity.h 在引擎子模块，SDL 头在 vendor
target_include_directories(collision_homing_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3_image/include")

# 链接主工程构建出的 SDL3 与 SDL3_image 导入库
#（Entity.cpp 引用 SDL_LogWarn 与 IMG_LoadTexture 两组符号）
target_link_libraries(collision_homing_test PRIVATE
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib"
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL3_image/SDL3_image.lib")

# 运行目录放 SDL3.dll 与 SDL3_image.dll（与主工程同一份构建产物）
add_custom_command(TARGET collision_homing_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3.dll"
    "$<TARGET_FILE_DIR:collision_homing_test>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3_image.dll"
    "$<TARGET_FILE_DIR:collision_homing_test>"
)

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(collision_homing_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  怪物攻击状态机单元测试（monster_attack_test，批四）
#
#  被测对象：game/Monster.h + Monster.cpp 的攻击部分——蓄力后倾 / 前扑
#  前冲 / 接触范围判定（碰撞盒 + 6px 余量）/ 冷却门控 / Hit-Miss 结算 /
#  池复用清态 / 方向零向量防御。
#
#  与 world_test 同模式：编译 Monster.cpp + Entity.cpp + World.cpp 并链接
#  SDL3 与 SDL3_image（日志 / 随机数 / 纹理符号）。实际跑到的路径不依赖
#  世界（World 指针为 nullptr 的防御分支）与纹理（不做渲染），任何环境
#  都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\monster_attack_test.exe
# ============================================================

add_executable(monster_attack_test
    monster_attack_test.cpp
    ../Src/cpp/game/Monster.cpp
    ../Src/cpp/game/World.cpp
    ../libs/GameEngine/src/Entity.cpp
)

if (MSVC)
    target_compile_options(monster_attack_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Monster.h 等游戏头在 Src/inc，引擎头在子模块，SDL 头在 vendor
target_include_directories(monster_attack_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3_image/include")

# 链接主工程构建出的 SDL3 与 SDL3_image 导入库
#（Monster/World/Entity 引用 SDL_Log、SDL_randf 与 IMG_LoadTexture 等符号）
target_link_libraries(monster_attack_test PRIVATE
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib"
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL3_image/SDL3_image.lib")

# 运行目录放 SDL3.dll 与 SDL3_image.dll（与主工程同一份构建产物）
add_custom_command(TARGET monster_attack_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3.dll"
    "$<TARGET_FILE_DIR:monster_attack_test>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3_image.dll"
    "$<TARGET_FILE_DIR:monster_attack_test>"
)

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(monster_attack_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  玩家受击反馈单元测试（player_hit_test，批五A）
#
#  被测对象：Src/cpp/game/Player.cpp 的受击击退部分——applyHitKnockback
#  的方向/初速/衰减推进、输入抑制窗口、碰撞滑动（贴墙截停）与重置归位。
#
#  与 world_test / monster_attack_test 同模式：编译 Player.cpp + World.cpp
#  + Entity.cpp + Input.cpp 并链接 SDL3 与 SDL3_image（日志/纹理/键盘查询
#  符号）。实际跑到的路径不加载纹理、不创建窗口；输入经 updateFromKeyArray
#  注入合成按键——任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\player_hit_test.exe
# ============================================================

add_executable(player_hit_test
    player_hit_test.cpp
    ../Src/cpp/game/Player.cpp
    ../Src/cpp/game/World.cpp
    ../libs/GameEngine/src/Entity.cpp
    ../libs/GameEngine/src/Input.cpp
)
if (MSVC)
    target_compile_options(player_hit_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Player.h 等游戏头在 Src/inc，引擎头在子模块，SDL 头在 vendor
target_include_directories(player_hit_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../libs/GameEngine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3_image/include")

# 链接主工程构建出的 SDL3 与 SDL3_image 导入库
#（Player/World/Entity 引用 SDL_Log 与 IMG_LoadTexture 等符号；
#  Input 引用 SDL_GetKeyboardState / SDL_GetMouseState）
target_link_libraries(player_hit_test PRIVATE
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL-main/SDL3.lib"
    "${TESTS_SDL3_BUILD_DIR}/vendor/SDL3_image/SDL3_image.lib")

# 运行目录放一份 SDL3.dll 与 SDL3_image.dll（与主工程同一份构建产物）
add_custom_command(TARGET player_hit_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3.dll"
    "$<TARGET_FILE_DIR:player_hit_test>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${TESTS_SDL3_BUILD_DIR}/SDL3_image.dll"
    "$<TARGET_FILE_DIR:player_hit_test>"
)

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(player_hit_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  法术池单元测试（spell_test，批五B）
#
#  被测对象：Src/inc/game/Spell.h（六法术表 / 逐层参数 / 层数工具 /
#  三抽抽取）+ Src/inc/game/ChoiceDraw.h（通用不重复抽取核心）+
#  Src/inc/game/UpgradeCard.h 的注入件（CardSeedRandom 种子流 /
#  LevelUpQueue 欠账队列——测试注入使用，行为回归另有 card_test）。
#
#  与 xp_test / card_test 同模式：不依赖 SDL3、无 .cpp、无链接，
#  随机源经注入固定序列/种子，任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\spell_test.exe
# ============================================================

add_executable(spell_test spell_test.cpp)

if (MSVC)
    target_compile_options(spell_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Spell.h / ChoiceDraw.h / UpgradeCard.h 位于游戏仓 Src/inc
target_include_directories(spell_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(spell_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  分区单元测试（zone_test，批六甲）
#
#  被测对象：Src/inc/game/Zone.h（区表 / zoneAt 查询 / 移速乘区）+ 
#  Src/inc/game/World.h 的几何常量（32 格区块 / 1536 像素）。
#  与 xp_test 同模式：纯头文件逻辑，只依赖 SDL 头（SDL.h 的 SDL_FRect），
#  不链接 SDL3 运行时——任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\zone_test.exe
# ============================================================

add_executable(zone_test zone_test.cpp)

if (MSVC)
    target_compile_options(zone_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Zone.h / World.h 位于游戏仓 Src/inc；
# SDL3 头目录供 World.h 的 SDL.h 依赖（仅类型与内联函数，不链接）
target_include_directories(zone_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(zone_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  商店单元测试（shop_test，批五B；批六甲加效果描述）
#
#  被测对象：Src/inc/game/Shop.h（价格公式与可购判定）+
#  Src/inc/game/ShopLayout.h（九宫格面板布局——渲染与鼠标命中同一份）+
#  Src/inc/game/ShopDesc.h（效果描述动态对比文案；批六甲）。
#
#  与 card_test 第 9 用例同模式：只依赖 SDL_rect.h 的类型与内联点测
#  （SDL_PointInRectFloat），不链接 SDL3 运行时——任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\shop_test.exe
# ============================================================

add_executable(shop_test shop_test.cpp)

if (MSVC)
    target_compile_options(shop_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Shop.h / ShopLayout.h 位于游戏仓 Src/inc；
# SDL3 头目录供 ShopLayout.h 的 SDL_rect.h 依赖（仅类型与内联函数）
target_include_directories(shop_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(shop_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)

# ============================================================
#  复活规则单元测试（respawn_test，批六乙）
#
#  被测对象：Src/inc/game/Shrine.h（神龛表 / 最近激活点 / 交互范围）+
#  Src/inc/game/RespawnRules.h（升级历史栈 / 死亡惩罚数学）。
#
#  与 zone_test 同模式：纯头文件逻辑，只依赖 SDL 头（Shrine.h 经 Zone.h/
#  World.h 使用 SDL 类型），不链接 SDL3 运行时——任何环境都能确定性运行。
#
#  构建运行命令（与其余测试同一工程）：
#      cmake -S tests -B tests/build
#      cmake --build tests/build --config Debug
#      tests\bin\respawn_test.exe
# ============================================================

add_executable(respawn_test respawn_test.cpp)

if (MSVC)
    target_compile_options(respawn_test PRIVATE /utf-8)
endif()

# 头文件搜索路径：Shrine.h / RespawnRules.h / Zone.h 位于游戏仓 Src/inc；
# SDL3 头目录供 Zone.h -> World.h 的 SDL.h 依赖（仅类型与内联函数，不链接）
target_include_directories(respawn_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Src/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL-main/include")

# 产物统一输出到 tests\bin\，与其余测试一致
set_target_properties(respawn_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
