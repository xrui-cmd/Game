#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// ============================================================
// Player：玩家实体（WASD 移动 + 行走帧动画）
// ============================================================
//
// 继承自 Entity 获得的能力（与 Coin 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - render()：按当前帧从精灵表裁帧绘制（本类无需覆写）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/player_sheet.png 2 帧行走精灵表（100x50），移动时播放；
//      次选 assets/player.png 单帧静态精灵（旧版素材，动画退化为静止）；
//      回退为程序化 50x50 绿色方块（素材缺失时兜底，保证游戏可运行）
//   2. 移动：WASD 移动，默认 300 像素/秒（可由配置覆盖），
//      边界按 UpdateContext 里的视口尺寸 clamp
//   3. 动画：移动才播、静止归位第 1 帧——所以覆写 update 而不用基类默认
//      （基类默认是「无条件推进时间轴」，适合金币那种一直旋转的实体）
// ============================================================

class Player : public Entity
{
public:
    // 配置项缺省值：与改造前的硬编码常量逐一相同。
    // PlayScene 读取 player_speed / player_anim_interval 时以它们作为回退
    static constexpr float kDefaultSpeed = 300.0f;    // 默认移动速度（像素/秒）
    static constexpr float kDefaultFrameTime = 0.15f; // 默认行走动画每帧时长（秒）

    // 构造：固定玩家方块尺寸为 50x50（与精灵表单帧尺寸一致）
    Player()
    {
        m_w = 50.0f; // 宽 50 像素：行走精灵表每帧宽 50px
        m_h = 50.0f; // 高 50 像素：行走精灵表每帧高 50px
    }

    // 加载玩家纹理：只声明素材路径、帧参数与回退颜色，
    // 三级回退（精灵表 -> 单帧图 -> 程序化纯色）流程全部收敛在基类
    bool load(SDL_Renderer* renderer);

    // 设置移动速度（像素/秒）：PlayScene 在 onEnter 时用配置值注入。
    // 负数会让 WASD 方向整体反转、属于配置错误，忽略并警告；0 允许（静止）
    void setSpeed(float speed);

    // 覆写更新：WASD 移动 + 「移动才播、静止归位第 1 帧」的行走动画。
    // 签名与基类保持一致：世界上下文（deltaTime/键盘/视口）打包传入
    void update(const UpdateContext& ctx) override;

    // 重开归位：把玩家瞬间移动到 (x, y)，内部委托基类 setPosition；
    // 保留这个语义化名字，让调用处（GameDev::restart）读起来更直观
    void reset(float x, float y);

private:
    float m_speed = kDefaultSpeed; // 移动速度（像素/秒）：onEnter 时由配置覆盖

    static constexpr int kFrameCount = 2; // 行走精灵表总帧数（2 帧 x 50px = 100px 宽）
};

#endif // !__PLAYER_H__
