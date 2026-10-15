#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画/翻转公共能力）

// ============================================================
// Player：玩家实体（WASD 移动 + 行走动画 + 自动攻击 + 武器挥击表现）
// ============================================================
//
// 继承自 Entity 获得的能力（与 Monster/XpGem 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - 水平翻转绘制：setFlipped（本批用于「移动朝向」）
//   - 动画循环子区间：setAnimationRange（本批用于「跑动帧/站立帧同表切换」）
//
// 本类自己的规则（本批：素材换血 + 自动攻击改版）：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/player_sheet.png 行走精灵表（5 帧，每帧 48x84）：
//      帧序 0~3 = 跑动动画（移动时循环播放），帧 4 = 站立姿态
//      （静止时经 setAnimationRange(4, 4) 定格）；帧序与组装脚本严格一致；
//      次选 assets/player.png 单帧静态精灵（动画退化为静止）；
//      回退为程序化 48x84 绿色方块（素材缺失时兜底，保证游戏可运行）
//   2. 移动（沿用「渐近速度模型 + 动作查询」）：目标速度 = 输入方向 x
//      player_speed，当前速度以 accel（有输入）/ friction（无输入）逐轴逼近；
//      「移动/静止」的判据是速度是否精确归零——动画切换与翻转都以它为准
//   3. 朝向翻转（本批新增）：水平输入决定左右翻转（向左镜像、向右原样），
//      静止时保持上一次朝向。翻转是纯渲染层标记：碰撞 rect()、插值位置与
//      攻击判定一律按未翻转的数据计算（引擎 setFlipped 的既有约定）
//   4. 自动攻击（本批新增，取代手动按 J 挥击的手砍式操作）：
//      - 分工：「场景选目标、玩家管出手」——PlayScene 每个固定步找出攻击
//        范围内最近的怪物后调用 tryAutoAttack(目标中心)；没有目标时场景
//        根本不会调用，「无目标不发招」由此天然成立；
//      - 冷却门控：冷却未到返回 false 不出手；出手则重置冷却并开始挥击表现；
//      - 命中结算在场景层（单体目标）：场景拿到 true 后对目标扣血/击退/掉宝；
//      - 冷却时长来自配置 attack_cooldown（setAttackCooldown 注入）
//   5. 武器表现（assets/weapon.png 骑士剑，可选增强）：
//      出手后约 kSwingSeconds 秒内，剑朝目标方向「短促前探 + 收回」
//      （sin 曲线位移），并叠加轻微旋转扫动；渲染在 render 覆写内完成。
//      武器素材加载失败只警告：攻击结算照常，只是没有挥剑画面
// ============================================================

class Player : public Entity
{
public:
    // 配置项缺省值：PlayScene 读取配置时以它们作为回退（缺键/坏值不致命）
    static constexpr float kDefaultSpeed = 300.0f;     // 默认移动速度（像素/秒）
    static constexpr float kDefaultFrameTime = 0.15f;  // 默认行走动画每帧时长（秒）
    static constexpr float kDefaultAccel = 1800.0f;    // 默认加速度（像素/秒²）：0->300 用时约 0.17 秒
    static constexpr float kDefaultFriction = 2400.0f; // 默认摩擦减速度（像素/秒²）：300->0 用时约 0.13 秒
    static constexpr float kDefaultAttackCooldown = 0.4f; // 默认自动攻击冷却（秒）：两次出手的最小间隔

    // 构造：固定玩家尺寸为 48x84（与精灵表单帧尺寸一致：16x28 像素 x 3 倍放大）
    Player()
    {
        m_w = 48.0f; // 宽 48 像素：骑士帧宽 16px x 3 倍放大（见 tools/compose_sprites.ps1）
        m_h = 84.0f; // 高 84 像素：骑士帧高 28px x 3 倍放大（含头盔羽饰的全身像）
    }

    // 析构：基类兜底释放本体纹理之外，本类还要释放自持的武器纹理
    ~Player() override;

    // 加载玩家纹理与武器素材：本体走基类三级回退（只声明路径/帧参数/回退色），
    // 武器为可选增强（失败仅警告）
    bool load(SDL_Renderer* renderer);

    // 设置移动速度（像素/秒）：PlayScene 在 onEnter 时用配置值注入。
    // 负数会让 WASD 方向整体反转、属于配置错误，忽略并警告；0 允许（静止）
    void setSpeed(float speed);

    // 设置加减速参数（像素/秒²）：PlayScene 在 onEnter 时用配置值注入。
    // accel   ：有输入时当前速度向目标速度靠拢的加速度；
    // friction：无输入时当前速度衰减到 0 的减速度。
    // 任一值为负属于配置错误：忽略整组并警告（加减速是一对参数，
    // 不作部分应用）；0 是合法值（无加速/无摩擦，显式配置的物理语义）
    void setMotion(float accel, float friction);

    // 设置自动攻击冷却（秒）：PlayScene 在 onEnter 时用配置值注入
    // （attack_cooldown，默认 0.4 秒）。负数属于配置错误：忽略并警告，
    // 保留原冷却；0 允许（每个固定步都出手的「无冷却」玩法）
    void setAttackCooldown(float seconds);

    // 覆写更新：动作查询驱动的渐近加减速移动 + 朝向翻转 + 攻击计时 +
    // 「移动播跑动帧、静止定格站立帧」的动画切换。方向输入来自 ctx.input
    // 的动作查询（held 移动）；签名与基类保持一致：世界上下文打包传入
    void update(const UpdateContext& ctx) override;

    // 覆写渲染：本体按基类插值绘制之后，挥击表现期内叠加绘制武器
    // （朝目标方向前探+收回；与本体共用同一条插值位置公式，快速移动不脱节）
    void render(SDL_Renderer* renderer, float alpha) const override;

    // 重开归位：把玩家瞬间移动到 (x, y) 并清零速度与攻击状态
    //（内部委托基类 setPosition，同时同步插值快照）；保留这个语义化名字
    // 让调用处（PlayScene::restart / onEnter）读起来更直观
    void reset(float x, float y);

    // ---- 自动攻击入口（PlayScene 索敌后调用）----

    // 尝试对目标自动出手：
    //   - 冷却未到：返回 false（本次不出手，场景不做命中结算）；
    //   - 冷却就绪：重置冷却、开始挥击表现（方向 = 玩家中心 -> 目标中心），
    //     返回 true——场景据此对目标结算伤害。
    // 目标中心与玩家中心重合（距离过近的退化情形）时方向取默认值（向右），
    // 避免归一化除以零产生 NaN
    bool tryAutoAttack(float targetCenterX, float targetCenterY);

private:
    // 释放武器纹理并置空（析构兜底；幂等）
    void releaseWeapon();

    // 插值后的玩家中心点（渲染坐标）：挥剑表现挂点用——与实体绘制使用
    // 同一条「prev -> cur 按 alpha 线性插值」公式
    SDL_FPoint interpolatedCenter(float alpha) const;

    float m_speed = kDefaultSpeed;         // 移动速度（像素/秒）：稳态速度上限，onEnter 时由配置覆盖
    float m_accel = kDefaultAccel;         // 加速度（像素/秒²）：有输入时的速度逼近速率
    float m_friction = kDefaultFriction;   // 摩擦减速度（像素/秒²）：无输入时的速度衰减速率

    // 当前速度（像素/秒），逐轴独立渐近：斜向输入时两轴各取自己的目标，
    // 与既有「未归一化直驱」的斜向速度语义保持一致（合速度约 1.41 倍）
    float m_velocityX = 0.0f; // 水平速度：正右负左
    float m_velocityY = 0.0f; // 垂直速度：正下负上（SDL y 轴向下为正）

    // ---- 自动攻击状态（本批新增）----

    // 挥击表现的几何常量（与 weapon.png 的尺寸/观感对应；改动需同步观感验收）
    static constexpr float kSwingSeconds = 0.12f;      // 挥击表现时长（秒）：前探+收回的总时间
    static constexpr float kWeaponAlong = 30.0f;       // 武器基准前移量（像素）：握柄离身体中心的距离
    static constexpr float kWeaponExtend = 30.0f;      // 前探最大追加位移（像素）：sin 曲线峰值
    static constexpr float kSwingSwayDegrees = 18.0f;  // 挥动轻微旋转幅度（度）：从 +18 扫到 -18
    static constexpr float kWeaponWidth = 30.0f;       // 武器绘制宽（像素）：与 weapon.png 尺寸一致
    static constexpr float kWeaponHeight = 87.0f;      // 武器绘制高（像素）：与 weapon.png 尺寸一致
    static constexpr float kDirectionEpsilon = 0.001f; // 方向归一化的退化阈值（距离小于它取默认方向）

    float m_attackCooldown = kDefaultAttackCooldown; // 自动攻击冷却总时长（秒）：onEnter 时由配置覆盖
    float m_attackCooldownLeft = 0.0f; // 冷却剩余（秒）：>0 时 tryAutoAttack 返回 false
    float m_swingLeft = 0.0f;          // 挥击表现剩余时长（秒）：>0 时 render 绘制挥剑
    float m_swingDirX = 1.0f;          // 本次挥击朝向 x 分量（单位向量，朝目标；初始向右）
    float m_swingDirY = 0.0f;          // 本次挥击朝向 y 分量（同上）

    SDL_Texture* m_weaponTexture = nullptr; // 武器纹理（assets/weapon.png；加载失败为 nullptr，挥剑跳过）

    // 行走精灵表的帧序约定（与 tools/compose_sprites.ps1 的组装顺序严格一致）
    static constexpr int kFrameCount = 5; // 精灵表总帧数（5 帧 x 48px = 240px 宽）
    static constexpr int kRunFirst = 0;   // 跑动子区间起点帧（移动时循环播放）
    static constexpr int kRunLast = 3;    // 跑动子区间终点帧（含）
    static constexpr int kIdleFrame = 4;  // 站立帧：静止时经 setAnimationRange 定格在这一帧
};

#endif // !__PLAYER_H__
