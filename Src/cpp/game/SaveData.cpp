#include "game/SaveData.h"
#include <SDL3/SDL.h>   // SDL_GetPrefPath / SDL_free / SDL_LoadFile / SDL_SaveFile / SDL_Log*
#include <charconv>     // std::from_chars：无异常的字符串 -> 整数严格解析
#include <string>       // std::string：路径与文件内容的存储
#include <string_view>  // std::string_view：逐行解析时不拷贝字节
#include <system_error> // std::errc：from_chars 的错误码类型

// ============================================================
// SaveData 的实现：定位用户数据目录、读三项纪录、写三项纪录。
// 头文件已说明设计背景，这里补充实现层面的关键点：
//   - 全部 SDL 调用集中在本文件（头文件零 SDL 依赖）；
//   - 解析采用「trim + 键匹配 + from_chars 全量消费」的严格校验；
//     认不得的键（含旧版 "highscore=" 行）静默忽略——旧文件因此天然
//     落到「无记录」分支，不需要任何专门的迁移代码；
//   - 写入固定三行 key=value + 换行，编辑器打开即可读。
// ============================================================

namespace
{

// 存档文件名（拼在 pref 目录之后的相对文件名）。
// 保留历史名称（见头文件的「为什么文件名仍叫 highscore.txt」）
constexpr const char* kFileName = "highscore.txt";

// 三个战绩键的键名（与头文件格式约定一一对应）
constexpr std::string_view kKeyKills = "best_kills";   // 最高击杀
constexpr std::string_view kKeyLevel = "best_level";   // 最高等级
constexpr std::string_view kKeyTime = "best_time";     // 最长存活（整秒）

// 去掉字符串首尾的空白字符（空格 / 制表符 / 回车 / 换行），返回值仍是
// 原文本的视图，不产生拷贝。存档是文本行格式但可能带 \r\n 行尾，
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

// 拼接最佳战绩存档的完整路径：<用户数据目录>/highscore.txt。
// 拿不到用户数据目录（SDL 创建失败，如磁盘异常）时返回空字符串，
// 由调用方按「读写失败」统一处理（函数内已记录具体警告）
std::string buildSavePath()
{
    // 取「每用户 + 每应用」的可写数据目录。SDL 首次调用时会自动创建
    // 目录；返回值是 SDL 分配的内存，必须由调用方 SDL_free（见头文件说明）
    char* prefPath = SDL_GetPrefPath("GameDev", "CoinRush");
    if (prefPath == nullptr)
    {
        // 无法定位用户数据目录：读写都走不通，记录原因后返回空路径
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "无法定位用户数据目录（%s）：最佳战绩存档不可用", SDL_GetError());
        return {};
    }

    // 拷贝进 std::string 后立刻归还 SDL 分配的内存：
    // 之后路径操作只依赖 std::string 自己管理的生命周期，杜绝泄漏
    std::string path = std::string(prefPath) + kFileName;
    SDL_free(prefPath); // 头文件注释要求：不再需要时用 SDL_free 释放

    return path; // 完整路径：<pref 目录>/highscore.txt
}

// 解析存档文本为最佳战绩快照。逐行扫描三行 key=value：
//   - 空行 / 认不得的键（含旧版 highscore= 行与其他垃圾行）→ 跳过；
//   - 认得的键但值非法（"abc" / "-5" / "12x"）→ 该键按 0 计并警告；
//   - 一个可识别键都没有 → 整体按「无记录」（三项全 0）并警告。
// 无论文件被怎么改乱，本函数都只返回合法快照、不抛异常、不崩溃
BestRecord parseRecord(std::string_view content)
{
    BestRecord record; // 全 0 起步：损坏路径天然落在「无记录」
    int validKeys = 0; // 成功解析的键数（0 = 整个文件按无记录处理）

    // 逐行扫描：存档行以 \n 分隔，最后一行可能没有换行符
    size_t pos = 0; // 当前行起点在原文中的下标
    while (pos < content.size())
    {
        // 本行结束位置：找到下一个换行（找不到 = 最后一行，取文本末尾）
        const size_t newline = content.find('\n', pos);
        const size_t lineEnd = (newline == std::string_view::npos) ? content.size() : newline;

        // 本行内容（去首尾空白）：CRLF 的 \r 在这里被一并去掉
        const std::string_view line = trim(content.substr(pos, lineEnd - pos));

        // 推进到下一行起点（最后一行扫描完后 pos 越过末尾，循环自然结束）
        pos = (newline == std::string_view::npos) ? content.size() : newline + 1;

        // 空行/纯空白行：跳过（不警告——空尾行是编辑器保存的常态）
        if (line.empty())
        {
            continue;
        }

        // 找到键值分隔符 '='；没有等号的行是垃圾行，静默跳过
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos)
        {
            continue;
        }

        // 键与值各自 trim（容忍 "best_kills = 3" 这类手写格式）
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view valueText = trim(line.substr(eq + 1));

        // 键名匹配三个战绩键：命中者得到对应字段的写入指针，其余键忽略
        // （旧版 "highscore=" 因此天然落在「认不得」分支——不迁移、不崩）
        int* target = nullptr; // 本行的目标字段（nullptr = 认不得的键）
        if (key == kKeyKills)
        {
            target = &record.kills; // 最高击杀
        }
        else if (key == kKeyLevel)
        {
            target = &record.level; // 最高等级
        }
        else if (key == kKeyTime)
        {
            target = &record.seconds; // 最长存活（整秒）
        }
        else
        {
            continue; // 认不得的键：跳过（正常运行不会刷警告）
        }

        // 严格解析为 int。from_chars 从首字符开始解析，因此 "abc"（解析失败）、
        // "12x"（只解析出前缀）都能被识别为非法；不用 atoi（静默返回 0，
        // 无法区分损坏）/ stoi（以异常报告失败）
        int value = 0; // 解析结果暂存（仅解析成功时写回字段）
        const char* begin = valueText.data();       // 文本起始指针
        const char* end = begin + valueText.size(); // 文本结束指针（开区间）
        const std::from_chars_result result = std::from_chars(begin, end, value);

        // 解析失败、存在残余字符、或得到负数：都视为该键损坏。
        // （三项纪录都为非负整数，出现负数说明内容不是本游戏写的）
        if (result.ec != std::errc{} || result.ptr != end || value < 0)
        {
            // 警告里带上键名：多键文件里能精确定位是哪一项被改坏
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "最佳战绩存档的键 %s 值非法，按 0 处理", std::string(key).c_str());
            continue; // 该字段保持 0，继续解析其余行
        }

        *target = value; // 解析成功：写入对应字段（重复键时后出现者覆盖）
        ++validKeys;     // 有效键计数 +1（用于「整体无记录」判定）
    }

    // 一个可识别键都没有（缺失内容 / 旧格式 / 全垃圾）：按无记录处理。
    // 这是正常降级路径而不是错误——游戏照常可玩、纪录从 0 起算
    if (validKeys == 0)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "最佳战绩存档无可识别内容（旧格式或损坏），按无记录处理");
    }

    return record; // 合法快照：三项要么是解析值、要么是 0
}

} // 匿名命名空间结束

// 读取最佳战绩：定位路径 -> 读完整文件 -> 逐行严格解析。
// 任何一步失败都记录警告并返回无记录快照，调用方拿到的一定是合法数值
BestRecord SaveData::load()
{
    // 第一步：拼接存档路径（内部已处理「拿不到目录」的警告）
    const std::string path = buildSavePath();
    if (path.empty())
    {
        return {}; // 无可用目录：按无记录处理（警告已由 buildSavePath 记录）
    }

    // 第二步：一次性读入整个文件。SDL_LoadFile 内部完成
    // 「打开 -> 读全部 -> 关闭」，返回内存块指针与字节数
    size_t fileSize = 0;
    void* rawData = SDL_LoadFile(path.c_str(), &fileSize);
    if (rawData == nullptr)
    {
        // 文件缺失（首次运行）或读取失败：按无记录处理。
        // 这是正常的降级路径而不是错误——游戏照常可玩、纪录从 0 起算
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "读取最佳战绩存档失败（%s），按无记录处理", SDL_GetError());
        return {};
    }

    // 字节块拷贝进 std::string 后立刻用 SDL_free 归还 SDL 分配的内存：
    // 后续解析只依赖 std::string 自己管理的生命周期
    const std::string content(static_cast<const char*>(rawData), fileSize);
    SDL_free(rawData);

    // 第三步：逐行严格解析（旧格式/损坏 -> 警告 + 无记录，见 parseRecord）
    const BestRecord record = parseRecord(content);

    // 冒烟观测点：自动化测试据此断言「启动时确实读取了存档」
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "最佳战绩存档已读取: 击杀 %d · 等级 %d · 存活 %d 秒",
                record.kills, record.level, record.seconds);
    return record; // 返回完整快照
}

// 写入最佳战绩：格式化三行文本 -> SDL_SaveFile 覆盖写出。
// 失败只警告并返回 false，游戏侧不受影响（内存里仍按新纪录展示）
bool SaveData::save(const BestRecord& record)
{
    // 第一步：拼接存档路径（内部已处理「拿不到目录」的警告）
    const std::string path = buildSavePath();
    if (path.empty())
    {
        return false; // 无可用目录：写盘必然失败（警告已记录）
    }

    // 第二步：格式化三行内容（固定行序 + 行尾 '\n'，便于编辑器阅读与
    // 人工检查；std::to_string 对 int 永不失败）
    const std::string text = std::string(kKeyKills) + "=" + std::to_string(record.kills) + "\n" +
                             std::string(kKeyLevel) + "=" + std::to_string(record.level) + "\n" +
                             std::string(kKeyTime) + "=" + std::to_string(record.seconds) + "\n";

    // 第三步：整块写出。SDL_SaveFile 内部以「写模式」打开文件
    // （不存在则创建、存在则截断重写）——旧格式文件也在这一步被覆盖干净
    if (!SDL_SaveFile(path.c_str(), text.data(), text.size()))
    {
        // 写盘失败（磁盘满/权限异常等）：只警告不中断游戏进程
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "写入最佳战绩存档失败（%s）", SDL_GetError());
        return false;
    }

    // 冒烟观测点：自动化测试据此断言「破纪录时立即写盘」
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "最佳战绩已写入: 击杀 %d · 等级 %d · 存活 %d 秒",
                record.kills, record.level, record.seconds);
    return true; // 写盘成功
}
