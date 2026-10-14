// ============================================================================
//  input_test.cpp — Input 输入系统单元测试
//
//  被测对象 : libs/GameEngine/include/engine/Input.h + libs/GameEngine/src/Input.cpp
//             （引擎子模块 libs/GameEngine，随 develop 子模块指针更新）
//  覆盖场景 : ① 未绑定动作恒 false（含空指针/空串容错）
//             ② 完整边沿序列：按下 -> 持续 -> 松开 -> 再按
//             ③ 改键覆盖：旧键失效、新键生效、不凭空产生「按下」
//             ④ 多动作相互独立（含同键绑定两个动作）
//
//  测试方式 : 用 updateFromKeyArray 注入合成键盘状态——不依赖游戏窗口与
//  真实键盘，任何时候都能确定性运行。Input.cpp 的 update() 路径引用
//  SDL_GetKeyboardState，因此需链接 SDL3 并携带 SDL3.dll 运行
//  （构建与运行方法见 tests/CMakeLists.txt）
//
//  注意 : CASE 1 会故意触发「空动作名 bind」警告，stderr 上出现 SDL 警告
//  属于预期行为
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "engine/Input.h" // 被测类型 Input（同时引入 SDL_scancode.h 的键位常量）

#include <cstdio>  // std::printf
#include <cstdlib> // std::system（Windows 控制台切 UTF-8）

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

// 断言：布尔条件成立
void expectTrue(const char* label, bool value)
{
  // 条件为 true 才算通过
  const bool ok = value;
  if (ok) { ++gPass; } else { ++gFail; }
  // 输出一行结果：PASS/FAIL + 用例名
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
}

// 断言：held / pressed / released 三元组完全符合期望。
// 三个查询一起断言：防止「held 正确但边沿错位」这类只查单项漏掉的问题
void expectState(const char* label, const Input& input, const char* action,
                 bool expectHeld, bool expectPressed, bool expectReleased)
{
  // 逐项取实测值（h=held、p=pressed、r=released）
  const bool held = input.held(action);
  const bool pressed = input.pressed(action);
  const bool released = input.released(action);
  // 三项全部一致才算通过
  const bool ok = (held == expectHeld) && (pressed == expectPressed) && (released == expectReleased);
  if (ok) { ++gPass; } else { ++gFail; }
  // 输出期望与实际（失败时一眼定位是哪一项没对上）
  std::printf("  [%s] %s | 期望 h=%d p=%d r=%d | 实际 h=%d p=%d r=%d\n",
              ok ? "PASS" : "FAIL", label,
              expectHeld ? 1 : 0, expectPressed ? 1 : 0, expectReleased ? 1 : 0,
              held ? 1 : 0, pressed ? 1 : 0, released ? 1 : 0);
}

void beginCase(int idx, const char* title)
{
  std::printf("\n[CASE %d] %s\n", idx, title);
}

void endCase(int idx, int passBefore, int failBefore)
{
  // 本用例新增的通过/失败数
  const int passed = gPass - passBefore;
  const int failed = gFail - failBefore;
  std::printf("  ---- CASE %d 小结: %d/%d 通过%s\n", idx, passed, passed + failed,
              failed ? "  <<<< 存在失败" : "");
}

// 合成键盘状态：布局与 SDL_GetKeyboardState 返回值同构（SDL_SCANCODE_COUNT 个键位）
struct FakeKeyboard
{
  bool keys[SDL_SCANCODE_COUNT] = {}; // 构造即全 false：与「应用刚启动」的键盘状态一致

  // 模拟「按住」某键：把该键位置为 true
  void press(SDL_Scancode key)
  {
    keys[key] = true; // 该键从下一帧采样起视为按住
  }

  // 模拟「松开」某键：把该键位置为 false
  void release(SDL_Scancode key)
  {
    keys[key] = false; // 该键从下一帧采样起视为松开
  }
};

} // namespace

int main()
{
#if defined(_WIN32)
  // 控制台代码页切到 UTF-8：中文输出与 SDL 警告不乱码（失败也不影响测试）
  std::system("chcp 65001 > nul 2>&1");
#endif

  std::printf("==============================================================\n");
  std::printf(" Input 输入系统单元测试 —— libs/GameEngine/include/engine/Input.h\n");
  std::printf(" 说明: 通过 updateFromKeyArray 注入合成键盘状态，无需窗口；\n");
  std::printf("       CASE 1 故意触发的空动作名警告会打印在下方，属预期输出\n");
  std::printf("==============================================================\n");

  // ------------------------------------------------------------------
  // CASE 1 未绑定动作：任何查询都恒 false（不崩、不警告）
  // ------------------------------------------------------------------
  beginCase(1, "未绑定动作 -> held/pressed/released 恒 false");
  int p0 = gPass, f0 = gFail;
  {
    Input input;     // 全新输入系统：没有任何绑定
    FakeKeyboard kb; // 全没按的合成键盘

    // 未绑定直接查询：三项都应为 false
    expectState("未绑定动作-初始查询", input, "no_such_action", false, false, false);

    // 注入一次「有键按下」的采样：未绑定动作不受影响
    kb.press(SDL_SCANCODE_W);          // 按下 W（与任何动作都无关）
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("未绑定动作-采样后查询", input, "no_such_action", false, false, false);

    // 空指针动作名：容错返回 false（不崩）
    expectTrue("空指针动作名 held", input.held(nullptr) == false);
    expectTrue("空指针动作名 pressed", input.pressed(nullptr) == false);
    expectTrue("空指针动作名 released", input.released(nullptr) == false);

    // 空字符串动作名：同样恒 false
    expectTrue("空串动作名 held", input.held("") == false);

    // bind 拒绝空动作名（警告属预期输出），被拒后依然查不到该动作
    input.bind("", SDL_SCANCODE_A);      // 空串动作名：应被忽略
    input.bind(nullptr, SDL_SCANCODE_A); // 空指针动作名：应被忽略
    expectState("空名 bind 后查询", input, "", false, false, false);
  }
  endCase(1, p0, f0);

  // ------------------------------------------------------------------
  // CASE 2 完整边沿序列：按下 -> 持续 -> 松开 -> 再按
  // ------------------------------------------------------------------
  beginCase(2, "边沿序列 -> 按下/持续/松开/再按的状态推进");
  p0 = gPass; f0 = gFail;
  {
    Input input;                          // 全新输入系统
    FakeKeyboard kb;                      // 合成键盘（全没按）
    input.bind("attack", SDL_SCANCODE_J); // 绑定动作 attack -> J 键

    // 第 1 帧：绑定后的初始采样（还没按键）
    input.updateFromKeyArray(kb.keys);
    expectState("第1帧-未按键", input, "attack", false, false, false);

    // 第 2 帧：刚按下 J -> pressed（上升沿）+ held
    kb.press(SDL_SCANCODE_J);          // 模拟按下 J
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("第2帧-刚按下", input, "attack", true, true, false);

    // 第 3 帧：持续按住 -> 仅 held（pressed 只出现一帧）
    input.updateFromKeyArray(kb.keys); // 键盘状态不变，仅再推进一帧
    expectState("第3帧-持续按住", input, "attack", true, false, false);

    // 第 4 帧：松开 J -> released（下降沿），held 取消
    kb.release(SDL_SCANCODE_J);        // 模拟松开 J
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("第4帧-刚松开", input, "attack", false, false, true);

    // 第 5 帧：持续松开 -> 全部 false（released 也只出现一帧）
    input.updateFromKeyArray(kb.keys); // 状态不变再推一帧
    expectState("第5帧-持续松开", input, "attack", false, false, false);

    // 第 6 帧：再次按下 -> 再次 pressed（边沿可重复触发）
    kb.press(SDL_SCANCODE_J);          // 再次模拟按下 J
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("第6帧-再次按下", input, "attack", true, true, false);
  }
  endCase(2, p0, f0);

  // ------------------------------------------------------------------
  // CASE 3 改键覆盖：旧键失效、新键生效、不凭空产生「按下」
  // ------------------------------------------------------------------
  beginCase(3, "改键覆盖 -> 旧键失效、新键生效、无凭空边沿");
  p0 = gPass; f0 = gFail;
  {
    Input input;                           // 全新输入系统
    FakeKeyboard kb;                       // 合成键盘
    input.bind("move_up", SDL_SCANCODE_W); // 初始绑定：动作 -> W

    // 按下 W：动作触发（按下瞬间）
    kb.press(SDL_SCANCODE_W);          // 模拟按下 W
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("改键前-W 按下生效", input, "move_up", true, true, false);

    // 改键到 K（此时 W 仍物理按住、K 没按）：旧键立即失效
    input.bind("move_up", SDL_SCANCODE_K); // bind 再次调用 = 改键覆盖
    input.updateFromKeyArray(kb.keys);     // 推进一帧采样
    expectState("改键后-旧键 W 失效", input, "move_up", false, false, false);

    // 按下新键 K：动作按新键触发（新键生效）
    kb.press(SDL_SCANCODE_K);          // 模拟按下 K
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("改键后-新键 K 生效", input, "move_up", true, true, false);

    // 改键回 W：W 在物理上一直按着，恢复映射后 held 立即为 true；
    // 但 pressed 不该出现——物理键「从未松开过」，没有新的上升沿
    input.bind("move_up", SDL_SCANCODE_W); // 改回 W（W 仍按住）
    input.updateFromKeyArray(kb.keys);     // 推进一帧采样
    expectState("改回 W-不凭空产生按下", input, "move_up", true, false, false);

    // 改回后松开 W：下降沿正常触发（映射恢复正常工作）
    kb.release(SDL_SCANCODE_W);        // 模拟松开 W
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("改回 W-松开正常触发", input, "move_up", false, false, true);
  }
  endCase(3, p0, f0);

  // ------------------------------------------------------------------
  // CASE 4 多动作相互独立：按键不串扰、同键多动作各自成立
  // ------------------------------------------------------------------
  beginCase(4, "多动作 -> 彼此独立（按键不串扰）");
  p0 = gPass; f0 = gFail;
  {
    Input input;                              // 全新输入系统
    FakeKeyboard kb;                          // 合成键盘
    input.bind("move_up", SDL_SCANCODE_W);    // 动作一：W
    input.bind("move_down", SDL_SCANCODE_S);  // 动作二：S

    // 只按 W：动作一触发、动作二完全不受影响
    kb.press(SDL_SCANCODE_W);          // 模拟按下 W
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("只按 W-move_up 触发", input, "move_up", true, true, false);
    expectState("只按 W-move_down 不受影响", input, "move_down", false, false, false);

    // 再按住 S：两个动作各自独立报告状态
    kb.press(SDL_SCANCODE_S);          // 模拟再按下 S（W 仍按住）
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("W+S-move_up 持续", input, "move_up", true, false, false);
    expectState("W+S-move_down 刚按下", input, "move_down", true, true, false);

    // 松开 W、保留 S：两个动作各自独立推进边沿
    kb.release(SDL_SCANCODE_W);        // 模拟松开 W（S 仍按住）
    input.updateFromKeyArray(kb.keys); // 推进一帧采样
    expectState("松 W-move_up 刚松开", input, "move_up", false, false, true);
    expectState("松 W-move_down 持续", input, "move_down", true, false, false);

    // 同一物理键绑定两个动作：两条映射条目互相独立、同时触发
    input.bind("confirm", SDL_SCANCODE_SPACE); // 动作三：空格
    input.bind("jump", SDL_SCANCODE_SPACE);    // 动作四：同为空格
    kb.press(SDL_SCANCODE_SPACE);              // 模拟按下空格
    input.updateFromKeyArray(kb.keys);         // 推进一帧采样
    expectState("同键-confirm 触发", input, "confirm", true, true, false);
    expectState("同键-jump 触发", input, "jump", true, true, false);
  }
  endCase(4, p0, f0);

  // ------------------------------------------------------------------
  const int total = gPass + gFail;
  std::printf("\n==============================================================\n");
  std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
  std::printf(" 结论: %s\n", (gFail == 0)
      ? "全部通过 —— Input 边沿检测与改键行为符合预期。"
      : "存在失败用例，请检查上方 [FAIL] 行。");
  std::printf(" main 返回值（失败用例数）: %d\n", gFail);
  std::printf("==============================================================\n");

  return gFail; // 失败数作为退出码：全部通过时为 0
}
