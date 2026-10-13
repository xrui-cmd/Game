#include "game/SaveData.h"
#include <SDL3/SDL.h>   // SDL_GetPrefPath / SDL_free / SDL_LoadFile / SDL_SaveFile / SDL_Log*
#include <charconv>     // std::from_chars：无异常的字符串 -> 整数严格解析
#include <string>       // std::string：路径与文件内容的存储
#include <string_view>  // std::string_view：解析时不拷贝字节
#include <system_error> // std::errc：from_chars 的错误码类型

// ============================================================
// SaveData 的实现：定位用户数据目录、读存档、写存档。
// 头文件已说明设计背景，这里补充实现层面的关键点：
//   - 全部 SDL 调用集中在本文件（头文件零 SDL 依赖）；
//   - 解析采用「trim + 前缀匹配 + from_chars 全量消费」的三步严格校验，
//     任何一步不满足都按「损坏」处理（警告 + 0），与 Config 的解析纪律一致；
//   - 写入带换行符 '\n'：文件保持「单行文本」格式，编辑器打开即可读。
// ============================================================

namespace
{

// 存档文件名（拼在 pref 目录之后的相对文件名）
constexpr const char* kFileName = "highscore.txt";

// 单行格式的键名前缀（含 '='）：解析时先做前缀匹配，确保内容来自本游戏
constexpr std::string_view kKeyPrefix = "highscore=";

// 去掉字符串首尾的空白字符（空格 / 制表符 / 回车 / 换行），返回值仍是
// 原文本的视图，不产生拷贝。存档是单行文本但可能带 \r\n 行尾，
// 解析前必须先归一化（与 Config.cpp 的同名工具同理，本编译单元私有）
std::string_view trim(std::string_view text)
{
    // 左端：从头部跳过空白；整串都是空白时 find_first_not_of 返回 npos
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
    {
        return std::string_view{}; // 整串都是空白：归一化为空视图，交给空内容分支
    }

    // 右端：从尾部跳过空白。begin 有效时 find_last_not_of 必然有效
    const size_t end = text.find_last_not_of(" \t\r\n");

    // substr(起点, 长度)：长度 = 末有效字符下标 - 首有效字符下标 + 1
    return text.substr(begin, end - begin + 1);
}

// 拼接最高分存档的完整路径：<用户数据目录>/highscore.txt。
// 拿不到用户数据目录（SDL 创建失败，如磁盘异常）时返回空字符串，
// 由调用方按「读写失败」统一处理（函数内已记录具体警告）
std::string buildHighScorePath()
{
    // 取「每用户 + 每应用」的可写数据目录。SDL 首次调用时会自动创建
    // 目录；返回值是 SDL 分配的内存，必须由调用方 SDL_free（见头文件说明）
    char* prefPath = SDL_GetPrefPath("GameDev", "CoinRush");
    if (prefPath == nullptr)
    {
        // 无法定位用户数据目录：读写都走不通，记录原因后返回空路径
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "无法定位用户数据目录（%s）：最高分存档不可用", SDL_GetError());
        return {};
    }

    // 拷贝进 std::string 后立刻归还 SDL 分配的内存：
    // 之后路径操作只依赖 std::string 自己管理的生命周期，杜绝泄漏
    std::string path = std::string(prefPath) + kFileName;
    SDL_free(prefPath); // 头文件注释要求：不再需要时用 SDL_free 释放

    return path; // 完整路径：<pref 目录>/highscore.txt
}

// 解析存档文本为最高分。按「损坏 -> 0」的契约记录具体原因的警告：
//   - 缺少 "highscore=" 前缀（内容被改乱 / 根本不是本游戏的文件）；
//   - 值不是合法非负整数（如 "highscore=abc"、"highscore=-5"、"12x"）
int parseHighScore(std::string_view content)
{
    // 先归一化首尾空白：容忍编辑器保存时追加的换行
    const std::string_view line = trim(content);

    // 第一步：前缀匹配。文件是单行格式，整段内容必须以此开头
    if (!line.starts_with(kKeyPrefix))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "最高分存档损坏（缺少 \"highscore=\" 前缀），按 0 处理");
        return 0; // 损坏容错：不崩，按 0 计
    }

    // 第二步：取 '=' 之后的值文本（再 trim，容忍 "highscore= 42 " 的手写格式）
    const std::string_view valueText = trim(line.substr(kKeyPrefix.size()));

    // 第三步：严格解析为 int。from_chars 从首字符开始解析，因此
    // "abc"（解析失败）、"12x"（只解析出前缀）都能被识别为非法；
    // 不用 atoi（静默返回 0，无法区分损坏）/ stoi（以异常报告失败）
    int value = 0;
    const char* begin = valueText.data();       // 文本起始指针
    const char* end = begin + valueText.size(); // 文本结束指针（开区间）
    const std::from_chars_result result = std::from_chars(begin, end, value);

    // 解析失败、存在残余字符、或得到负数：都视为损坏。
    // （游戏内得分不可能为负，出现负数说明内容不是本游戏写的）
    if (result.ec != std::errc{} || result.ptr != end || value < 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "最高分存档损坏（值不是合法非负整数），按 0 处理");
        return 0; // 损坏容错：不崩，按 0 计
    }

    return value; // 解析成功：返回存档中的最高分
}

} // 匿名命名空间结束

// 读取历史最高分：定位路径 -> 读整个文件 -> 严格解析。
// 任何一步失败都记录警告并返回 0，调用方拿到的一定是合法数值
int SaveData::loadHighScore()
{
    // 第一步：拼接存档路径（内部已处理「拿不到目录」的警告）
    const std::string path = buildHighScorePath();
    if (path.empty())
    {
        return 0; // 无可用目录：按 0 处理（警告已由 buildHighScorePath 记录）
    }

    // 第二步：一次性读入整个文件。SDL_LoadFile 内部完成
    // 「打开 -> 读全部 -> 关闭」，返回内存块指针与字节数
    size_t fileSize = 0;
    void* rawData = SDL_LoadFile(path.c_str(), &fileSize);
    if (rawData == nullptr)
    {
        // 文件缺失（首次运行）或读取失败：按 0 处理。
        // 这是正常的降级路径而不是错误——游戏照常可玩、纪录从 0 起算
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "读取最高分存档失败（%s），按 0 处理", SDL_GetError());
        return 0;
    }

    // 字节块拷贝进 std::string 后立刻用 SDL_free 归还 SDL 分配的内存：
    // 后续解析只依赖 std::string 自己管理的生命周期
    const std::string content(static_cast<const char*>(rawData), fileSize);
    SDL_free(rawData);

    // 第三步：严格解析（损坏 -> 警告 + 0，见 parseHighScore）
    const int highScore = parseHighScore(content);

    // 冒烟观测点：自动化测试据此断言「启动时确实读取了存档」
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "最高分存档已读取: %d", highScore);
    return highScore;
}

// 写入历史最高分：格式化单行文本 -> SDL_SaveFile 覆盖写出。
// 失败只警告并返回 false，游戏侧不受影响（内存里仍按新纪录展示）
bool SaveData::saveHighScore(int score)
{
    // 第一步：拼接存档路径（内部已处理「拿不到目录」的警告）
    const std::string path = buildHighScorePath();
    if (path.empty())
    {
        return false; // 无可用目录：写盘必然失败（警告已记录）
    }

    // 第二步：格式化单行内容 "highscore=N\n"。
    // std::to_string 对 int 永不失败；'\n' 让文件以空行收尾、便于编辑器阅读
    const std::string text = std::string(kKeyPrefix) + std::to_string(score) + "\n";

    // 第三步：整块写出。SDL_SaveFile 内部以「写模式」打开文件
    // （不存在则创建、存在则截断重写），与 SDL_LoadFile 配对使用
    if (!SDL_SaveFile(path.c_str(), text.data(), text.size()))
    {
        // 写盘失败（磁盘满/权限异常等）：只警告不中断游戏进程
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "写入最高分存档失败（%s）", SDL_GetError());
        return false;
    }

    // 冒烟观测点：自动化测试据此断言「破纪录时立即写盘」
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "最高分存档已写入: %d", score);
    return true; // 写盘成功
}
