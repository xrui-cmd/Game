#include "game/LevelupSfx.h"
#include <string> // std::string：拼接音效文件的绝对路径

// ============================================================
// LevelupSfx 的实现：一次性 WAV 的「加载 -> 惰性开流 -> 播放」。
// 头文件已说明过渡性设计与降级契约，这里补充实现层面的关键点：
//   - 全部 SDL 音频调用集中在本文件（头文件只声明类型与语义）；
//   - play 的日志先于一切判断：事件挂接正确性与设备可用性相互独立，
//     自动化冒烟据此断言「升级触发了音效事件」；
//   - 释放顺序：先销毁流（会一并关闭其绑定的设备）、再释放 PCM，
//     最后配对退出音频子系统——与创建顺序严格互逆。
// ============================================================

// 析构兜底：即调用方忘记显式清理，流与 PCM 也不会泄漏
LevelupSfx::~LevelupSfx()
{
    release(); // 内部逐项判空，未 load/load 失败时是安全空操作
}

// 释放全部资源并复位状态（幂等：可重复调用）
void LevelupSfx::release()
{
    // 先销毁播放流：SDL_OpenAudioDeviceStream 建立的「简化流」在销毁时
    // 会一并关闭其关联的音频设备（SDL 官方约定，见 SDL_audio.h 注释）
    if (m_stream != nullptr)
    {
        SDL_DestroyAudioStream(m_stream); // 停止播放并关闭设备
        m_stream = nullptr;               // 置空：二次调用时跳过
    }

    // 再释放 PCM 数据（SDL_LoadWAV 用 SDL_malloc 分配，必须 SDL_free）
    if (m_pcm != nullptr)
    {
        SDL_free(m_pcm);    // 归还解码缓冲
        m_pcm = nullptr;    // 置空：二次调用时跳过
        m_pcmLen = 0;       // 长度一并清零，避免悬挂的旧字节数
    }

    // 最后配对退出音频子系统：只在确实初始化过时调用（引用计数语义）
    if (m_audioSubsystemHeld)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO); // 与 play 中的 InitSubSystem 配对
        m_audioSubsystemHeld = false;      // 复位标记：允许将来重新初始化
    }

    // 禁用标记一并复位：release 后再 load 是「重新开始」的语义
    m_disabled = false;
}

// 加载升级音：定位素材 -> SDL_LoadWAV 整段解码到内存（不打开设备）。
// 失败只警告并返回 false——升级静默，但不影响升级本身
bool LevelupSfx::load()
{
    release(); // 幂等：重复 load 先释放旧资源，避免泄漏与状态混叠

    // 素材定位与其余资源同一模式：以 exe 所在目录为基准拼绝对路径
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr)
    {
        // 拿不到程序目录：记录原因后放弃（play 时只记事件日志不出声）
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "升级音效不可用：无法定位程序目录（%s）", SDL_GetError());
        return false;
    }

    // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
    const std::string path = std::string(basePath) + "assets/sounds/levelup.wav";

    // SDL_LoadWAV：读文件 -> 解析 WAV 头 -> 整段 PCM 拷进新分配的缓冲。
    // 成功时 m_spec 描述采样格式、m_pcm/m_pcmLen 持有数据（本类负责释放）
    if (!SDL_LoadWAV(path.c_str(), &m_spec, &m_pcm, &m_pcmLen))
    {
        // 素材缺失/损坏：只警告一次并继续——升级反馈的其余部分照常
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载升级音效失败（%s）：升级将静默", SDL_GetError());
        m_pcm = nullptr; // 防御：确认失败时缓冲指针为空（release 依赖判空）
        return false;
    }

    // 冒烟观测点：确认素材就绪（设备是否可用由首次 play 时决定）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "升级音效已加载: %u 字节（%d Hz）", m_pcmLen, m_spec.freq);
    return true; // PCM 就绪
}

// 播放一次：日志先行（观测点）-> 惰性开流 -> 清残 -> 入队 -> 恢复播放
void LevelupSfx::play()
{
    // 事件日志先于一切判断：与引擎 AudioSystem::play 的观测点契约一致——
    // 「事件挂接是否正确」与「音频设备是否可用」相互独立（冒烟断言用）
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "播放音效: levelup");

    // 无 PCM 数据（未 load / load 失败 / 已禁用）：静默返回。
    // load 与首败时已警告过，这里不重复刷屏
    if (m_pcm == nullptr || m_pcmLen == 0 || m_disabled)
    {
        return;
    }

    // 惰性开流：首次播放才真正打开默认播放设备。之前一直没播过的话，
    // 设备与音频子系统此前完全未涉及（不升级的会话零音频开销）
    if (m_stream == nullptr)
    {
        // SDL_OpenAudioDeviceStream 要求音频子系统已初始化（见 SDL 源码
        // 「Audio subsystem is not initialized」检查），因此先显式初始化。
        // SDL 子系统初始化是引用计数：与引擎 AudioSystem 的初始化互不冲突
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            // 无音频环境：警告一次并整体禁用（与引擎音效的降级策略同款）
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "升级音效播放不可用：音频子系统初始化失败（%s）", SDL_GetError());
            m_disabled = true; // 后续 play 直接跳过
            return;
        }
        m_audioSubsystemHeld = true; // 记账：release 时配对 QuitSubSystem

        // 按 WAV 的格式描述打开「流 -> 默认播放设备」的绑定。
        // callback 传 nullptr：由本类用 PutAudioStreamData 主动喂数据
        m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &m_spec, nullptr, nullptr);
        if (m_stream == nullptr)
        {
            // 开设备失败（设备被独占/驱动异常等）：警告一次并整体禁用
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "升级音效播放不可用：打开音频流失败（%s）", SDL_GetError());
            m_disabled = true; // 后续 play 直接跳过
            return;
        }
    }

    // 清掉上一段未播完的残留：重复触发（连升多级）时从头播新的一次。
    // 失败不致命：最多听感上叠一次旧音，玩法不受影响
    SDL_ClearAudioStream(m_stream);

    // PCM 整段入队（拷贝进流的内部缓冲；长度参数为 int，转换见下）。
    // 素材为几十 KB 的短音效，int 范围天然安全（防御性转换）
    if (!SDL_PutAudioStreamData(m_stream, m_pcm, static_cast<int>(m_pcmLen)))
    {
        // 入队失败（内存/设备异常）：只警告首次，避免连续升级刷屏
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "升级音效入队失败（%s）", SDL_GetError());
        return;
    }

    // 恢复播放：SDL_OpenAudioDeviceStream 建立时默认是暂停态（与 SDL2 语义
    // 对齐），每次入队后调用本函数确保设备在跑；已恢复时重复调用无害
    SDL_ResumeAudioStreamDevice(m_stream);
}
