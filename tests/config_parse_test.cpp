// ============================================================================
//  config_parse_test.cpp — Config 配置解析器单元测试
//
//  被测对象 : Src/inc/engine/Config.h + Src/cpp/engine/Config.cpp
//  覆盖场景 : 正常键值 / 注释行 / 空行 / 坏行 / 缺键默认 / 非法值默认 /
//             文件缺失 / 空白与 CRLF / 重复键覆盖 / UTF-8 BOM
//
//  与 rect_test 的差异 : 本测试需要链接 SDL3（Config 内部用 SDL_LoadFile
//  读文件、SDL_LogWarn 记录警告），构建与运行方法见 tests/CMakeLists.txt
//
//  注意 : 测试会故意触发缺键/非法值路径，stderr 上出现 SDL 警告属于预期行为
//
//  退出码 : main 返回"失败用例数"（全部通过 = 0）
// ============================================================================

#include "engine/Config.h" // 被测类型 Config

#include <cmath>    // std::fabs：浮点近似比较
#include <cstdio>   // std::printf / std::remove
#include <cstdlib>  // std::system
#include <fstream>  // std::ofstream：写临时配置文件
#include <string>   // std::string：配置文本

namespace
{

int gPass = 0; // 通过的断言数
int gFail = 0; // 失败的断言数（作为 main 返回值）

const char* kTmpPath = "config_parse_test_tmp.ini"; // 临时配置文件路径（测试结束删除）

// 写一个临时配置文件：二进制模式保证 \n 不被 Windows 文本模式改写
void writeFile(const char* path, const std::string& content)
{
    // 二进制模式打开：字节级精确写入，CRLF 用例才能构造真实的 \r\n 行尾
    std::ofstream out(path, std::ios::binary);
    // 整块写出全部字节；流对象析构时自动关闭文件
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// 删除临时文件（文件不存在时静默忽略）
void removeFile(const char* path)
{
    std::remove(path); // C 标准库删除；失败也不影响测试结论
}

// 断言：整数相等
void expectInt(const char* label, int got, int expect)
{
    // 实际值与期望值一致才算通过
    const bool ok = (got == expect);
    if (ok) { ++gPass; } else { ++gFail; }
    // 输出一行结果：PASS/FAIL + 用例名 + 期望与实际
    std::printf("  [%s] %s | 期望=%d 实际=%d\n", ok ? "PASS" : "FAIL", label, expect, got);
}

// 断言：浮点数近似相等（解析结果与字面量之间允许极小舍入误差）
void expectFloat(const char* label, float got, float expect)
{
    // 误差小于 1e-6 视为相等：float 只有约 7 位有效数字
    const bool ok = std::fabs(got - expect) < 1e-6f;
    if (ok) { ++gPass; } else { ++gFail; }
    std::printf("  [%s] %s | 期望=%g 实际=%g\n", ok ? "PASS" : "FAIL", label,
                static_cast<double>(expect), static_cast<double>(got));
}

// 断言：布尔条件成立
void expectTrue(const char* label, bool value)
{
    // 条件为 true 才算通过
    const bool ok = value;
    if (ok) { ++gPass; } else { ++gFail; }
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
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

} // namespace

int main()
{
#if defined(_WIN32)
    // 控制台代码页切到 UTF-8：中文输出与 SDL 警告不乱码（失败也不影响测试）
    std::system("chcp 65001 > nul 2>&1");
#endif

    std::printf("==============================================================\n");
    std::printf(" Config 配置解析器单元测试 —— Src/inc/engine/Config.h\n");
    std::printf(" 说明: 故意触发的缺键/非法值警告会打印在下方，属于预期输出\n");
    std::printf("==============================================================\n");

    // ------------------------------------------------------------------
    // CASE 1 正常键值：7 个业务键按类型读回（游戏实际用到的键）
    // ------------------------------------------------------------------
    beginCase(1, "正常键值 -> 按类型读回配置值");
    int p0 = gPass, f0 = gFail;
    {
        Config config; // 全新解析器实例
        // 写入一份与 config/game.ini 同构的完整配置
        writeFile(kTmpPath,
                  "window_width=800\n"
                  "window_height=600\n"
                  "player_speed=150\n"
                  "player_anim_interval=0.3\n"
                  "coin_count=3\n"
                  "coin_anim_interval=0.24\n"
                  "round_seconds=5\n");
        // 加载必须成功（文件存在且格式合法）
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        // 逐键校验：读取值与写入值一致
        expectInt("window_width=800", config.getInt("window_width", 1280), 800);
        expectInt("window_height=600", config.getInt("window_height", 720), 600);
        expectInt("player_speed=150", config.getInt("player_speed", 300), 150);
        expectFloat("player_anim_interval=0.3", config.getFloat("player_anim_interval", 0.15f), 0.3f);
        expectInt("coin_count=3", config.getInt("coin_count", 5), 3);
        expectFloat("coin_anim_interval=0.24", config.getFloat("coin_anim_interval", 0.12f), 0.24f);
        expectFloat("round_seconds=5（整数写法按浮点读）", config.getFloat("round_seconds", 30.0f), 5.0f);
    }
    endCase(1, p0, f0);

    // ------------------------------------------------------------------
    // CASE 2 注释行与空行：; 与 # 开头、空行、纯空白行全部静默跳过
    // ------------------------------------------------------------------
    beginCase(2, "注释行与空行 -> 跳过且不影响正常键");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        writeFile(kTmpPath,
                  "; 这是 ; 注释\n"
                  "# 这是 # 注释\n"
                  "\n"
                  "   \n"
                  ";coin_count=99\n"
                  "coin_count=7\n");
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        // 被注释掉的 coin_count=99 不生效，实际读到 7
        expectInt("注释掉的键不生效", config.getInt("coin_count", 5), 7);
    }
    endCase(2, p0, f0);

    // ------------------------------------------------------------------
    // CASE 3 坏行：缺少 '='、键名为空 -> 跳过并继续解析其余行
    // ------------------------------------------------------------------
    beginCase(3, "坏行 -> 跳过，好行不受影响（触发警告属于预期）");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        writeFile(kTmpPath,
                  "this line has no equals sign\n"
                  "=5\n"
                  "good_key=42\n");
        // 坏行只是被跳过，整体加载仍算成功
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        // 坏行里的键查不到，回退默认值
        expectInt("无等号的行被跳过", config.getInt("this line has no equals sign", -1), -1);
        // 正常行照常生效
        expectInt("坏行之后的好行照常解析", config.getInt("good_key", -1), 42);
    }
    endCase(3, p0, f0);

    // ------------------------------------------------------------------
    // CASE 4 缺键：请求未配置的键 -> 返回调用方默认值
    // ------------------------------------------------------------------
    beginCase(4, "缺键 -> 回退调用方默认值（触发警告属于预期）");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        writeFile(kTmpPath, "only_key=1\n");
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        expectInt("不存在的 int 键", config.getInt("no_such_key", 123), 123);
        expectFloat("不存在的 float 键", config.getFloat("no_such_float", 2.5f), 2.5f);
    }
    endCase(4, p0, f0);

    // ------------------------------------------------------------------
    // CASE 5 非法值：非数字 / 数字带尾巴 / 空值 -> 返回默认值
    // ------------------------------------------------------------------
    beginCase(5, "非法值 -> 回退默认值（触发警告属于预期）");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        writeFile(kTmpPath,
                  "bad_int=abc\n"
                  "bad_int_tail=12x\n"
                  "bad_float=.\n"
                  "empty_value=\n");
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        // 纯字母不是整数
        expectInt("bad_int=abc", config.getInt("bad_int", 99), 99);
        // "12x" 后半段不是数字：严格解析拒绝它
        expectInt("bad_int_tail=12x", config.getInt("bad_int_tail", 99), 99);
        // "." 不是合法浮点
        expectFloat("bad_float=.", config.getFloat("bad_float", 1.5f), 1.5f);
        // 空值同样视为非法
        expectFloat("empty_value=", config.getFloat("empty_value", 1.5f), 1.5f);
    }
    endCase(5, p0, f0);

    // ------------------------------------------------------------------
    // CASE 6 文件缺失：loadFromFile 返回 false，键查询全部回退默认值
    // ------------------------------------------------------------------
    beginCase(6, "文件缺失 -> 加载失败但查询安全（触发警告属于预期）");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        // 确保文件不存在
        removeFile(kTmpPath);
        // 文件缺失时明确返回 false
        expectTrue("loadFromFile 返回 false", config.loadFromFile(kTmpPath) == false);
        // 失败后仍可安全查询：得到默认值而非崩溃
        expectInt("缺失文件后查询 int", config.getInt("window_width", 1280), 1280);
        expectFloat("缺失文件后查询 float", config.getFloat("round_seconds", 30.0f), 30.0f);
    }
    endCase(6, p0, f0);

    // ------------------------------------------------------------------
    // CASE 7 空白与 CRLF：等号两侧空格、行尾 \r、行尾空格都能正确解析
    // ------------------------------------------------------------------
    beginCase(7, "空白与 CRLF -> 键值两侧去空白");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        // \r\n 行尾（Windows 记事本默认）+ 等号两侧空格 + 行尾空格
        writeFile(kTmpPath,
                  "  spaced_key  =  42  \r\n"
                  "crlf_key=7\r\n");
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        expectInt("等号两侧空白被去掉", config.getInt("spaced_key", -1), 42);
        expectInt("CRLF 行尾的 \\r 被去掉", config.getInt("crlf_key", -1), 7);
    }
    endCase(7, p0, f0);

    // ------------------------------------------------------------------
    // CASE 8 重复键：同一键出现两次 -> 后出现者生效
    // ------------------------------------------------------------------
    beginCase(8, "重复键 -> 后出现者覆盖先出现者");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        writeFile(kTmpPath,
                  "dup_key=1\n"
                  "dup_key=2\n");
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        expectInt("同键后者胜出", config.getInt("dup_key", -1), 2);
    }
    endCase(8, p0, f0);

    // ------------------------------------------------------------------
    // CASE 9 UTF-8 BOM：文件头带 BOM 时第一个键仍能解析
    // ------------------------------------------------------------------
    beginCase(9, "UTF-8 BOM -> 首个键不被 BOM 污染");
    p0 = gPass; f0 = gFail;
    {
        Config config;
        // 先写 BOM 三字节，再写普通键值行
        writeFile(kTmpPath, std::string("\xEF\xBB\xBF") + "bom_key=9\n");
        expectTrue("loadFromFile 返回 true", config.loadFromFile(kTmpPath));
        expectInt("BOM 之后的键可读", config.getInt("bom_key", -1), 9);
    }
    endCase(9, p0, f0);

    // 收尾：删除临时文件，不留垃圾
    removeFile(kTmpPath);

    // ------------------------------------------------------------------
    const int total = gPass + gFail;
    std::printf("\n==============================================================\n");
    std::printf(" 总计: %d 条断言 | 通过 %d | 失败 %d\n", total, gPass, gFail);
    std::printf(" 结论: %s\n", (gFail == 0)
        ? "全部通过 —— Config 解析行为符合预期。"
        : "存在失败用例，请检查上方 [FAIL] 行。");
    std::printf(" main 返回值（失败用例数）: %d\n", gFail);
    std::printf("==============================================================\n");

    return gFail; // 失败数作为退出码：全部通过时为 0
}
