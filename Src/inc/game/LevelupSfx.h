#ifndef __LEVELUPSFX_H__
#define __LEVELUPSFX_H__

#include <SDL3/SDL.h> // SDL_AudioSpec / SDL_AudioStream / Uint8：音频流播放的类型

// ============================================================
// LevelupSfx：升级音效播放器（game 层，直读 assets/sounds/levelup.wav）
// ============================================================
//
// 为什么游戏层自建播放器（过渡性设计，重要）：
//   引擎音效系统（engine/AudioSystem）是强类型 6 音效的固定枚举
//   （Coin/Start/GameOver/Pause/Hit/Record），没有 LevelUp；本批约束
//   「引擎仓不动」，因此升级音无法经 AudioSystem::play 播出来。
//   本类用 SDL3 核心音频流 API（SDL_LoadWAV + SDL_OpenAudioDeviceStream，
//   与 SDL3_mixer 无关、无新链接依赖）在游戏层补一个自包含的一次性
//   播放器——等引擎侧新增 SoundId::LevelUp 后，本类即可整体退役替换。
//
// 播放语义（与 AudioSystem 的轨道行为对齐）：
//   - 一次性播放：播完输出静音，不循环；
//   - 重复触发：清空未播完的残留数据后从头重播（连升多级时的听感衔接）。
//
// 失败降级（全链路静默，绝不因音频问题崩游戏）：
//   - 文件缺失/损坏：load 警告一次，play 静默（只记事件日志）；
//   - 无音频设备/开流失败：首次 play 警告一次并整体禁用，后续静默；
//   - 无论音频链路是否可用，play 都会记录「播放音效: levelup」日志——
//     与引擎 AudioSystem 的观测点同款，供冒烟测试断言事件挂接正确。
//
// 生命周期：
//   - 所有权语义：随所属场景构造/析构（RAII），析构自动关闭流并释放 PCM；
//   - 音频设备流在首次播放时惰性打开：不升级的会话不白开设备；
//   - 析构必须先于 SDL_Quit（场景在 GameDev::shutdown 里先于 SDL_Quit 销毁）。
// ============================================================

class LevelupSfx
{
public:
    LevelupSfx() = default; // 初始为空：load 之后才持有 PCM 数据

    // 析构兜底：释放音频流与 PCM 缓冲（幂等，见 release）
    ~LevelupSfx();

    // 禁止拷贝：持有独占的 PCM 缓冲与音频流，浅拷贝会造成双重释放
    LevelupSfx(const LevelupSfx&) = delete;
    LevelupSfx& operator=(const LevelupSfx&) = delete;

    // 加载 assets/sounds/levelup.wav（以 exe 目录为基准拼绝对路径）。
    // 返回是否拿到可用 PCM：失败只警告并返回 false（升级静默，不影响玩法）
    bool load();

    // 播放一次：任何环节失败都静默降级、不崩、不阻塞；每次调用都会写
    // 「播放音效: levelup」事件日志（冒烟观测点，先于一切判断）
    void play();

private:
    // 释放音频流与 PCM 缓冲并复位状态（幂等：析构与重复 load 都会调用）
    void release();

    SDL_AudioSpec m_spec{};   // WAV 的格式描述（SDL_LoadWAV 成功时回填）
    Uint8* m_pcm = nullptr;   // 解码后的 PCM 数据（SDL_LoadWAV 分配，SDL_free 释放）
    Uint32 m_pcmLen = 0;      // PCM 数据字节数

    // 播放流（惰性创建）：SDL_OpenAudioDeviceStream 建立「流 -> 默认播放
    // 设备」的绑定；一旦创建就持续复用（每次播放仅清残、入队、恢复）
    SDL_AudioStream* m_stream = nullptr;

    // 音频子系统持有标记：本类调用过 SDL_InitSubSystem(SDL_INIT_AUDIO) 后
    // 置位，释放时配对 SDL_QuitSubSystem（SDL 子系统初始化是引用计数）
    bool m_audioSubsystemHeld = false;

    // 禁用标记：音频子系统初始化失败/开流失败后置位——后续 play 直接跳过，
    // 不再反复尝试（首败时已警告一次）
    bool m_disabled = false;
};

#endif // !__LEVELUPSFX_H__
