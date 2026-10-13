#ifndef __SAVEDATA_H__
#define __SAVEDATA_H__

// ============================================================
// SaveData：最高分存档的读写（game 层，纯文件 IO 工具类）
// ============================================================
//
// 存储位置（关键设计）：
//   <SDL_GetPrefPath("GameDev", "CoinRush")>/highscore.txt
//   SDL_GetPrefPath 返回「每用户 + 每应用」专属的可写数据目录
//   （Windows 下形如 C:\Users\<用户>\AppData\Roaming\GameDev\CoinRush\），
//   首次调用时 SDL 会自动创建目录，返回值保证以路径分隔符结尾。
//
// 为什么用户数据不写 exe 目录（SDL_GetBasePath() 所在处）？
//   1. exe 目录可能只读：游戏安装在 Program Files 等受保护位置时，
//      普通用户进程没有写权限，写存档必然失败；
//   2. 多用户共用一台机器时：exe 目录是共享的，A 玩出的纪录会被
//      后一个人覆盖，每个人的个人数据互相冲突；
//   3. 升级/卸载/重新解压游戏时 exe 目录常被整体替换或删除，
//      存在里面的存档会随程序一起丢失。
//   SDL 官方对 SDL_GetPrefPath 的注释也明确「应假定 pref path 是
//   唯一安全的写入位置」（见 vendored 头文件 SDL_filesystem.h）。
//
// 内存所有权（以 vendored 头文件的注释为准）：
//   vendor/SDL-main/include/SDL3/SDL_filesystem.h 中 SDL_GetPrefPath
//   的注释写明：“This should be freed with SDL_free() when it is no
//   longer needed.”——返回值是 SDL 分配的 char*，调用方用完后必须用
//   SDL_free 释放。实现里先把内容拷贝进 std::string，再立刻 SDL_free，
//   既避免泄漏又让后续路径操作走 C++ 字符串（见 SaveData.cpp）。
//
// 文件格式：单行文本 "highscore=123\n"。
// 用 SDL_LoadFile 读、SDL_SaveFile 写（vendored SDL_iostream.h 提供，
// 均为 SDL3.2+ 的标准文件整块读写 API）。
//
// 容错契约（与项目「资源缺失不崩」的一贯策略一致）：
//   - 读：文件缺失/内容损坏 → 记录警告并返回 0；
//   - 写：拿不到目录/写盘失败 → 记录警告并返回 false；
//   两种情况都不抛异常、不中断游戏。
// ============================================================

class SaveData
{
public:
    // 读取历史最高分。文件缺失（首次运行）或内容损坏（缺少 "highscore="
    // 前缀 / 值不是合法非负整数）时记录警告并返回 0——
    // 调用方永远拿到一个合法数值，无需自行容错
    static int loadHighScore();

    // 覆盖写入历史最高分（单行 "highscore=N"）。返回是否成功：
    // 失败时记录警告并返回 false，游戏侧不受影响（内存里仍按新纪录展示，
    // 只是下次启动会丢失该纪录）
    static bool saveHighScore(int score);
};

#endif // !__SAVEDATA_H__
