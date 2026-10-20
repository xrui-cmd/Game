#ifndef __PLAYER_H__
#define __PLAYER_H__

#include "engine/Entity.h"     // Entity：实体基类（空间/纹理/帧动画/翻转公共能力）
#include "engine/math/Vec2.h"  // Vec2：挥砍方向（单位向量）的类型化返回
#include "game/Dodge.h"        // DodgeRules：冲刺时长/距离/冷却/方向/禁触发纯逻辑（M3·丙）

// 前置声明：世界（碰撞查询）与相机（渲染换算）在本类中只以指针/引用出现，
// 完整定义分别由 Player.cpp 的包含获得（减少头依赖）
class World;
class Camera;

// ============================================================
// Player：玩家实体（WASD 移动 + 行走动画 + 主动攻击 + 武器横扫表现）
// ============================================================
//
// 继承自 Entity 获得的能力（与 Monster/XpGem 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - 水平翻转绘制：setFlipped（本批用于「朝向鼠标」）
//   - 动画循环子区间：setAnimationRange（本批用于「跑动帧/站立帧同表切换」）
//
// 本类自己的规则（本批：战斗改版——主动攻击 / 鼠标瞄准 / 横扫）：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/player_sheet.png 行走精灵表（5 帧，每帧 48x84）：
//      帧序 0~3 = 跑动动画（移动时循环播放），帧 4 = 站立姿态
//      （静止时经 setAnimationRange(4, 4) 定格）；帧序与组装脚本严格一致；
//      次选 assets/player.png 单帧静态精灵（动画退化为静止）；
//      回退为程序化 48x84 绿色方块（素材缺失时兜底，保证游戏可运行）
//   2. 移动（沿用「渐近速度模型 + 动作查询」）：目标速度 = 输入方向 x
//      player_speed，当前速度以 accel（有输入）/ friction（无输入）逐轴逼近；
//      「移动/静止」的判据是速度是否精确归零——动画切换以它为准。
//      自批二起：位置推进改为「分轴位移 + 撞墙滑动」（World::slideMove），
//      取代旧版「窗口边框 clamp」——玩家活动范围 = 世界边界（由边界墙
//      自然形成），碰撞判定在实体内部、按注入的 World 执行。
//      批六乙：玩家改走 World::slideMovePlayer——水域可通行（入水后由
//      PlayScene 切换 Drowning/落水流），墙与怪物仍为实心。
//      自批四起（碰撞体积与视觉解耦）：滑动判定改用「脚部碰撞盒」
//      （宽 32 x 高 26、底部内收 4px，构造时 setCollisionBox 设置）——
//      精灵身体（48x84 全身像）可以自然叠压墙脚，脚盒不穿透；渲染/
//      朝向/武器挂点照旧用完整精灵（rect），判定与画面由此解耦
//   3. 朝向翻转（批二改版）：跟随鼠标——指针在玩家中心右侧朝右、左侧朝左；
//      指针贴近中心线（±kFlipDeadZone 像素）时保持上一次朝向，避免抖动。
//      鼠标位置由场景注入「世界坐标」（setAimTarget）——场景负责
//      屏幕->世界的相机换算（坐标换算属场景层职责，玩家不依赖相机）；
//      翻转是纯渲染层标记：碰撞 rect()、插值位置与攻击判定一律按未翻转的
//      数据计算（引擎 setFlipped 的既有约定）
//   4. 主动攻击（本批核心，取代自动索敌）：PlayScene 每个固定步把「鼠标
//      瞄准方向（玩家中心 -> 鼠标）」交给 tryAttack——冷却就绪则出手并
//      启动横扫表现（返回 true），冷却未到返回 false。玩家不关心场上
//      有没有怪物：挥空也出招、不索敌（「有没有目标」只影响场景层的
//      命中结算，不影响是否出手）；命中结算在场景层（扇形多目标，
//      判定走 AttackArc 纯函数）；冷却时长来自配置 attack_cooldown
//      （setAttackCooldown 注入）
//   5. 武器表现（assets/weapon.png 骑士剑，可选增强）：
//      出手后 attack_swing_time 秒内（默认 0.10），剑以「玩家中心下移
//      kOrbitPivotOffsetY 像素」为旋转锚点、沿瞄准方向从 −半张角扫到
//      +半张角（扫角 = 攻击弧 attack_arc）；扫程位置走缓出曲线
//      （easeOutSwingProgress：快起慢收），与场景的逐帧命中判定共用
//      AttackArc.h 的纯函数（画到哪、打到哪）。主体剑之后拖 3 道
//      渐隐残影（角度落后 8°/16°/24°，alpha 0.55/0.30/0.15）。
//      渲染在 render 覆写内完成；自批二起渲染位置经相机换算到屏幕
//      （批二的贴图未变，只是坐标系从「窗口」升级为「世界」）。
//      武器素材加载失败只警告：攻击结算照常，只是没有横扫画面
//   6. 受击反馈（批五A 改版）：被怪物攻击命中时由场景调用
//      applyHitKnockback——沿「伤害来源 -> 玩家」方向被推开：
//        - 击退：初速 kHitKnockbackSpeed（480px/s）、指数衰减
//          （时间常数 kHitKnockbackTau）、历时 kHitKnockbackSeconds
//          （0.18s）、总位移约 42px（规格 40~50px）；位移经世界碰撞
//          滑动（墙 + 实心盒），撞墙滑动不穿墙；
//        - 输入抑制：击退窗口内 WASD 移动输入被抑制（约 0.18s，
//          与击退同窗）；受击瞬间常规速度清零（冲击手感），
//          窗口结束输入恢复、从静止重新加速；
//        - 红染：受击后 kHitTintSeconds（0.3s）内 render 叠加红光
//          （ADD，强度 = kHitTintGreenBlue 可调）；
//        - 旧版「无敌闪烁（渲染跳隐）」已移除：无敌时长本身保留在
//          场景层（0.8s，防连续扣血），但渲染不再做任何闪烁
//   7. 角色闪避（M3·丙·空格冲刺）：PlayScene 在 Playing 状态检测
//      bind_dodge（默认 SPACE）的按下边缘，按「WASD 合成方向优先、
//      无输入时玩家中心 -> 鼠标、退化时沿用朝向」选出方向后调用
//      tryDodge——本类负责门控（冻结/冷却/已冲刺）、固定方向、按
//      DodgeRules 的二次缓出曲线推进位移（0.18s / 120px，走
//      World::slideMovePlayer：撞墙该轴即停、可滑动），并在冲刺期间
//      渲染 3 重渐隐精灵残影。冲刺无敌由场景判定（dodging()），与受击
//      0.8s 无敌窗口互相独立；冷却从成功触发的那一步起算 1.2s。
//      冲刺距离不受玩家移速/雪地/减速乘区影响（速度曲线只依赖
//      DodgeRules 的常量）；冻结会立即打断冲刺（防御路径）。
// ============================================================

class Player : public Entity
{
public:
    // 配置项缺省值：PlayScene 读取配置时以它们作为回退（缺键/坏值不致命）
    static constexpr float kDefaultSpeed = 300.0f;     // 默认移动速度（像素/秒）
    static constexpr float kDefaultFrameTime = 0.15f;  // 默认行走动画每帧时长（秒）
    static constexpr float kDefaultAccel = 1800.0f;    // 默认加速度（像素/秒²）：0->300 用时约 0.17 秒
    static constexpr float kDefaultFriction = 2400.0f; // 默认摩擦减速度（像素/秒²）：300->0 用时约 0.13 秒
    static constexpr float kDefaultAttackCooldown = 0.4f; // 默认攻击冷却（秒）：两次出手的最小间隔
    static constexpr float kDefaultSwingTime = 0.10f;     // 默认扫程时长（秒）：刀锋从 −半张角 扫到 +半张角 的总时间

    // 构造：固定玩家尺寸为 48x84（与精灵表单帧尺寸一致：16x28 像素 x 3 倍放大）
    Player()
    {
        m_w = 48.0f; // 宽 48 像素：骑士帧宽 16px x 3 倍放大（见 tools/compose_sprites.ps1）
        m_h = 84.0f; // 高 84 像素：骑士帧高 28px x 3 倍放大（含头盔羽饰的全身像）

        // 脚部碰撞盒（批四·碰撞体积与视觉解耦）：48x84 全身像中只取脚部
        // 32x26 的小盒——宽居中（右移 8px）、底边内收 4px（上移 54px）；
        // 墙滑动/落点/玩家↔怪物触碰/攻击距离都改用它（渲染照旧全身像）
        setCollisionBox(kCollisionBoxW, kCollisionBoxH,
                        kCollisionBoxOffsetX, kCollisionBoxOffsetY);
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

    // 设置攻击冷却（秒）：PlayScene 在 onEnter 时用配置值注入
    // （attack_cooldown，默认 0.4 秒）。负数属于配置错误：忽略并警告，
    // 保留原冷却；0 允许（每个固定步都出手的「无冷却」玩法）
    void setAttackCooldown(float seconds);

    // 设置扫程时长（秒）：PlayScene 在 onEnter 时用配置值注入
    // （attack_swing_time，默认 0.10 秒）——出手后刀锋扫完全程的总时间，
    // 也是扫程进度归一化的分母（判定与渲染共用）。非正值属于配置错误：
    // 忽略并警告（0 时长会让进度归一化失去分母；与冷却的 0「无冷却」
    // 语义不同——扫程 0 时长没有可表达的表现）
    void setSwingTime(float seconds);

    // 设置扫角（度）：PlayScene 在 onEnter 时用配置值注入（attack_arc，
    // 默认 120 度）——刀锋从「瞄准角 − 半张角」扫到「瞄准角 + 半张角」
    // 的跨度，与场景扇形判定的张角是同一个值（画到哪、打到哪的结构性
    // 保证）。负值属于配置错误：忽略并警告；0 允许（刀锋原地不扫，
    // 判定退化为中轴线上的点）
    void setAttackArc(float degrees);

    // 注入世界指针（场景在 onEnter 时调用；只借用不持有）：
    // 移动推进经 World::slideMove 执行「分轴位移 + 撞墙滑动 + 子步防穿透」。
    // 传 nullptr 视为「无墙世界」（防御——未注入时位移不限制、不崩溃）
    void setWorld(const World* world) { m_world = world; }

    // 注入「额外实心盒」数组（批四·体积碰撞）：场景每固定步刷新一次
    // （当前为存活怪物们的碰撞盒快照）——玩家移动把它们当「墙」一样对待
    // （撞上滑停、不可重合；由 World::slideMoveEx 的额外实心判据执行）。
    // 只借用不持有：数组内存由场景保证贯穿整个 update 周期；
    // rects 为空指针 / count <= 0 视为「无实心体」（等价旧行为）
    void setSolidRects(const SDL_FRect* rects, int count)
    {
        m_solidRects = rects; // 数组首地址（只读借用）
        m_solidCount = count; // 元素个数（<=0 = 无实心体）
    }

    // 注入瞄准目标（鼠标的「世界坐标」）：场景每固定步把鼠标屏幕位置经
    // camera.screenToWorld 换算后传来——玩家不依赖相机类型，坐标系换算
    // 收敛在场景层。朝向翻转以它为基准（死区逻辑不变）；从未注入过时
    // 保持朝向不动（与旧版「无输入系统不翻转」等价）
    void setAimTarget(float worldX, float worldY)
    {
        m_aimX = worldX; // 瞄准点世界坐标 x（翻转判定的横向基准）
        m_aimY = worldY; // 瞄准点世界坐标 y（参与「指针位置」语义的完整性）
        m_hasAim = true; // 标记：此后 update 的翻转判定参考它
    }

    // 覆写更新：动作查询驱动的渐近加减速移动（经世界碰撞滑动）+
    // 朝向跟随注入的瞄准点 + 攻击计时 + 「移动播跑动帧、静止定格
    // 站立帧」的动画切换。方向输入来自 ctx.input 的动作查询（held 移动）；
    // 签名与基类保持一致
    void update(const UpdateContext& ctx) override;

    // 覆写渲染：本体按基类插值绘制（世界坐标 -> 相机换算）之后，
    // 横扫表现期内叠加绘制武器（绕玩家中心从 −60° 扫到 +60°；
    // 武器锚点与本体共用同一条插值位置公式，再经同一相机换算——
    // 快速移动不脱节、相机跟随不漂移）。
    // 批六乙：落水表现期（m_drownAlpha < 255 或 m_drownSinkY > 0）改走
    // 「下沉 + 淡出」的本体绘制（不画武器/受击红染——死亡过场优先）
    void render(SDL_Renderer* renderer, float alpha, const Camera& camera) const override;

    // 设置落水表现（批六乙）：sinkPixels = 相对逻辑位置的下沉像素（>=0），
    // opacity01 = 本体不透明度 [0,1]。场景在 Drowning 每步按进度调用；
    // 复活/重开时传 (0, 1) 复位。参数越界自动夹取，纯渲染层状态——
    // 碰撞/位置/判定一律不受影响
    void setDrownVisual(float sinkPixels, float opacity01);

    // 立即取消横扫表现（批六乙·落水/死亡过场）：把表现计时清零——武器与
    // 残影下一帧起不再绘制，避免世界冻结后挥砍画面永久停留在屏幕上。
    // 只影响表现，不改冷却/方向/命中记录（场景侧扫程状态另行复位）
    void cancelSwingVisual();

    // 设置冻结锁定（批六丁·冻结条满 100）：true = 不可移动/不可攻击。
    // 冻结期间 update 跳过输入与位移（速度/击退残余清零），render 叠加
    // 「蓝白裹冰」视觉；场景同时跳过普攻与法术施放。false = 解除锁定。
    // 纯状态开关：冻结时长与抗冻缓冲由 ElementStatus::FreezeMeter 负责
    void setFrozen(bool frozen) { m_frozen = frozen; }

    // 是否处于冻结锁定（场景/HUD 查询）
    bool frozen() const { return m_frozen; }

    // 重开归位：把玩家瞬间移动到 (x, y) 并清零速度与攻击状态
    //（内部委托基类 setPosition，同时同步插值快照）；保留这个语义化名字
    // 让调用处（PlayScene::restart / onEnter）读起来更直观
    void reset(float x, float y);

    // ---- 角色闪避入口（M3·丙；PlayScene 在 Playing 检测按下边缘后调用）----

    // 尝试开始冲刺：方向由场景经 DodgeRules::selectDirection 选好（不必
    // 归一化，内部会再收敛一次）。返回阻塞原因（None = 成功启动）：
    //   - Frozen：冻结中（Player 自身状态；场景不重复判定）；
    //   - Dashing：本次冲刺尚未结束；
    //   - Cooldown：冲刺冷却剩余 > 0。
    // 场景只负责「是否 Playing」——非 Playing 不会调用本函数。
    // 成功时：固定方向、清常规速度与击退、置满冷却（1.2s，从本步起算）、
    // 重置残影轨迹；冲刺期内 update 跳过常规 WASD 位移，改走二次缓出曲线
    DodgeRules::DodgeStartBlock tryDodge(float dirX, float dirY);

    // 是否处于冲刺中：场景据此跳过怪物攻击伤害与元素累积（冲刺无敌），
    // 渲染据此绘制残影；冲刺结束的那一步起返回 false
    bool dodging() const { return m_dodgeActive; }

    // 本次（最近一次）冲刺的实际位移（像素；撞墙滑动/提前结束会小于 120）
    // 场景在冲刺结束的固定步读取并打日志——冒烟位移证据的数值来源
    float dodgeDistance() const { return m_dodgeTravelled; }

    // 冲刺冷却剩余（秒；0 = 就绪）：场景日志与冷却提示使用
    float dodgeCooldownLeft() const { return m_dodgeCooldownLeft; }

    // 立即取消冲刺（冻结打断/防御路径）：只清冲刺状态与残影，不清冷却。
    // 正常冲刺无敌期内不会吃到冻结；本入口保证异常时序下也不会带着
    // 冲刺状态被锁定或复活
    void cancelDodge();

    // ---- 受击反馈入口（批五A，PlayScene 在怪物攻击命中时调用）----

    // 受击击退 + 红染 + 输入抑制（契约见头文件顶部第 6 条）：
    //   - 方向 = 来源中心 (fromCenterX, fromCenterY) -> 玩家中心（「远离
    //     伤害来源」的被推开方向）；两中心完全重合（零向量退化）时
    //     回退朝 +x（确定性方向，不产生 NaN）；
    //   - 击退速度置满初速（480px/s），update 内指数衰减推进、0.18s 到期
    //     清零（位移经世界碰撞滑动——撞墙滑动不穿墙）；
    //   - 常规速度清零（受击冲击）——击退窗口内输入被抑制，速度保持 0；
    //   - 红染计时置满（0.3s，render 叠加红光）。
    // 连续受击：全部状态重新计时（以最后一次受击为准）
    void applyHitKnockback(float fromCenterX, float fromCenterY);

    // ---- 主动攻击入口（PlayScene 每固定步在「左键按住」时调用）----

    // 尝试朝瞄准方向出手：
    //   - 冷却未到：返回 false（本次不出手，场景不做命中结算）；
    //   - 冷却就绪：归一化方向、重置冷却、启动横扫表现（方向 = 瞄准方向，
    //     即「玩家中心 -> 鼠标」），返回 true——场景据此做扇形结算。
    // aimDirection 不必归一化（内部换算）；长度过小（零向量退化）时保留
    // 上一次的挥砍方向，避免 NaN 污染旋转角。出手与「有没有怪物」无关：
    // 挥空也出招、也消耗冷却（主动输入的直接结果）
    bool tryAttack(float aimDirX, float aimDirY);

    // 本次（最近一次）挥砍的朝向：单位向量。挥砍表现与场景的扇形判定
    // 共用它——场景在 tryAttack 成功后取回这个方向做弧内判定，保证
    // 「判定中轴」与「画面中轴」严格同一。从未出手过时为初始朝向（向右）
    Vec2 swingDirection() const { return Vec2{ m_swingDirX, m_swingDirY }; }

    // 扫程是否进行中：render 画剑/残影、PlayScene 逐帧判定都以它为准
    // （出手后 m_swingLeft 从满值递减到 0 的整个窗口）
    bool swingActive() const { return m_swingLeft > 0.0f; }

    // 本次扫程的归一化进度 [0,1]：0 = 刚出手，1 = 扫程结束。
    // 判定（PlayScene 求相邻两步扫过的角区间）与渲染（画剑朝向）共用
    // 它换算刀锋角（bladeAngleAt）——两侧因此严格同一条曲线、同一相位；
    // 不在扫程中时按「已完成」返回 1
    float swingProgress01() const;

    // 插值后的玩家中心点（渲染坐标）：横扫表现挂点用——与实体绘制使用
    // 同一条「prev -> cur 按 alpha 线性插值」公式。
    // 批五B 起公开：法术特效（灼烧光环/护盾罩的挂点）需要与玩家绘制
    // 严格同源的插值中心——各算各的会随调参漂移（同 CardLayout 的纪律）
    SDL_FPoint interpolatedCenter(float alpha) const;

private:
    // 释放武器纹理并置空（析构兜底；幂等）
    void releaseWeapon();

    float m_speed = kDefaultSpeed;         // 移动速度（像素/秒）：稳态速度上限，onEnter 时由配置覆盖
    float m_accel = kDefaultAccel;         // 加速度（像素/秒²）：有输入时的速度逼近速率
    float m_friction = kDefaultFriction;   // 摩擦减速度（像素/秒²）：无输入时的速度衰减速率

    // ---- 世界与瞄准（批二新增；场景注入）----
    const World* m_world = nullptr; // 世界指针（碰撞查询；nullptr = 无墙世界，防御）
    float m_aimX = 0.0f;            // 瞄准点世界坐标 x（setAimTarget 写入）
    float m_aimY = 0.0f;            // 瞄准点世界坐标 y
    bool m_hasAim = false;          // 是否已注入过瞄准点（false = 翻转判定保持朝向）

    // ---- 体积碰撞（批四）：额外实心盒（场景注入的怪物盒快照）----
    const SDL_FRect* m_solidRects = nullptr; // 实心盒数组（场景持有；只读借用；nullptr = 无）
    int m_solidCount = 0;                    // 数组长度（<=0 = 无实心体）

    // slideMoveEx 回调（静态成员满足 C 函数指针签名；context 传 this）：
    // 候选矩形与任一实心盒重叠即不可推进——「怪物盒为实心」的实现
    static bool solidBlockedCallback(void* context, const SDL_FRect& rect);

    // 落水表现期的本体绘制（批六乙）：与 Entity::render 同一帧号/插值/
    // 翻转算法，额外叠加「下沉像素」与纹理 alpha 调制；调用后恢复 alpha，
    // 不污染后续绘制。只在 m_drownAlpha < 255 或 m_drownSinkY > 0 时走这里
    void renderDrownBody(SDL_Renderer* renderer, float alpha, const Camera& camera) const;

    // 冻结裹冰视觉（批六丁）：在本体之上叠加半透明冰蓝方罩 + 亮白边框 +
    // 三道斜冰纹（纯 SDL 图元，无新素材）。位置与本体同一条插值公式，
    // 绘制后恢复混合模式（不污染后续绘制）。只在 m_frozen 时调用
    void renderFreezeOverlay(SDL_Renderer* renderer, float alpha, const Camera& camera) const;

    // 冲刺残影（M3·丙）：在本体之前绘制 3 道历史位置精灵——复用武器残影
    // 的「近浓远淡」思路（α 0.45/0.28/0.13），颜色略偏青蓝以区分实体。
    // 只在 m_dodgeActive 时绘制；每道都经相机换算、沿用当前帧/翻转状态，
    // 绘制后恢复纹理颜色/alpha（防污染本体与后续实体）
    void renderDodgeGhosts(SDL_Renderer* renderer, const Camera& camera) const;

    // 当前速度（像素/秒），逐轴独立渐近：斜向输入时两轴各取自己的目标，
    // 与既有「未归一化直驱」的斜向速度语义保持一致（合速度约 1.41 倍）
    float m_velocityX = 0.0f; // 水平速度：正右负左
    float m_velocityY = 0.0f; // 垂直速度：正下负上（SDL y 轴向下为正）

    // ---- 主动攻击状态（本批改版）----

    // 横扫表现的几何常量（与 weapon.png 的尺寸/观感对应；改动需同步观感验收）
    static constexpr float kDefaultSweepArcDegrees = 120.0f; // 默认扫角（度）：config attack_arc 的缺省值（与场景扇形张角同值）
    static constexpr float kWeaponOrbit = 32.0f;       // 武器中心绕旋转锚点的轨道半径（像素）：本批 40 -> 32（支点修正）
    static constexpr float kOrbitPivotOffsetY = 8.0f;  // 旋转锚点相对玩家中心的垂直偏移（像素，向下为正）：本批新增（支点修正）
    static constexpr float kWeaponWidth = 30.0f;       // 武器绘制宽（像素）：与 weapon.png 尺寸一致
    static constexpr float kWeaponHeight = 87.0f;      // 武器绘制高（像素）：与 weapon.png 尺寸一致
    static constexpr float kDirectionEpsilon = 0.001f; // 方向归一化的退化阈值（长度小于它保留旧方向）
    static constexpr float kFlipDeadZone = 4.0f;       // 朝向跟随鼠标的死区（像素）：横向差小于它时保持朝向

    // ---- 受击反馈参数（批五A；代码常量，不进配置——可调、改注释同步）----
    static constexpr float kHitKnockbackSpeed = 480.0f;  // 击退初速（像素/秒）——规格约 480
    static constexpr float kHitKnockbackTau = 0.11f;     // 指数衰减时间常数（秒）：总位移 ≈ 480 x 0.11 x (1-e^(-0.18/0.11)) ≈ 42px（规格 40~50）
    static constexpr float kHitKnockbackSeconds = 0.18f; // 击退/输入抑制时长（秒）——规格约 0.18
    static constexpr float kHitTintSeconds = 0.3f;       // 红染时长（秒）——规格约 0.3（比击退略长，受击信号更醒目）
    static constexpr int kHitTintGreenBlue = 60;         // 红染强度：ADD 叠加前的绿/蓝通道（0~255，越低越红）——强度可调的落点

    // ---- 脚部碰撞盒（批四·碰撞体积与视觉解耦）----
    // 取值（像素）：32x26 的小盒摆在精灵底部——宽居中（偏移 (48-32)/2 = 8）、
    // 底边内收 4px（偏移 84-26-4 = 54）。可调：改这四个常量即改判定体积
    static constexpr float kCollisionBoxW = 32.0f;       // 碰撞盒宽（像素）
    static constexpr float kCollisionBoxH = 26.0f;       // 碰撞盒高（像素）
    static constexpr float kCollisionBoxOffsetX = 8.0f;  // 盒左上角相对精灵左上角的 x 偏移
    static constexpr float kCollisionBoxOffsetY = 54.0f; // 同上 y 偏移（下移到脚部；底部内收 4px）

    // 残影轨迹参数（本批新增）：主体剑之后拖 3 道渐隐剑影，角度依次
    // 落后于当前刀锋——读作「刚才扫过的几个位置」；绘制在主体剑之下、
    // alpha 从近到远递减。索引 0 对应最靠近主体的一道
    static constexpr int kTrailCount = 3;                                 // 残影数量（道）
    static constexpr float kTrailLagDegrees[3] = { 8.0f, 16.0f, 24.0f };  // 各道残影相对当前刀锋的角度落后量（度）
    static constexpr float kTrailAlphas[3] = { 0.55f, 0.30f, 0.15f };     // 各道残影的透明度（近 -> 远，越远越淡）

    // ---- 冲刺残影参数（M3·丙；代码常量）----
    // 3 道历史精灵：位置每隔 kDodgeTrailSampleSeconds 采样一次（0.03s ≈ 4 个
    // 固定步），α 近浓远淡；颜色偏青蓝，读作「高速位移的残像」
    static constexpr int kDodgeTrailCount = 3;                          // 残影数量（道）
    static constexpr float kDodgeTrailSampleSeconds = 0.03f;            // 残影采样间隔（秒）
    static constexpr float kDodgeTrailAlphas[3] = { 0.45f, 0.28f, 0.13f }; // 各道透明度（近 -> 远）

    // ---- 冲刺运行时（M3·丙；reset/冻结打断清零）----
    // 位移采用「累计曲线差分」：每步距离 = distanceAtProgress(新进度) -
    // distanceAtProgress(旧进度)，因此总位移精确等于 120px（无累积误差）
    struct DodgeTrailPoint
    {
        float x = 0.0f;      // 历史位置左上角 x（世界坐标）
        float y = 0.0f;      // 历史位置左上角 y（世界坐标）
        bool used = false;   // 该槽位是否已有有效采样（冲刺开始时填充起点）
    };

    bool m_dodgeActive = false;       // 是否处于冲刺中（无敌/残影/位移门控）
    float m_dodgeElapsed = 0.0f;      // 冲刺已进行时长（秒；进度 = 它 / 0.18）
    float m_dodgeCooldownLeft = 0.0f; // 冲刺冷却剩余（秒；>0 不可再冲刺）
    float m_dodgeDirX = 1.0f;         // 本次冲刺固定方向 x（单位向量；启动时锁定）
    float m_dodgeDirY = 0.0f;         // 本次冲刺固定方向 y（同上）
    float m_dodgeTravelled = 0.0f;    // 本次冲刺实际累计位移（像素；撞墙会减少）
    float m_dodgeTrailTimer = 0.0f;   // 残影采样计时（秒；达到间隔即推进轨迹）
    DodgeTrailPoint m_dodgeTrail[kDodgeTrailCount]; // 残影轨迹（下标 0 = 最新）

    float m_attackCooldown = kDefaultAttackCooldown; // 攻击冷却总时长（秒）：onEnter 时由配置覆盖
    float m_attackCooldownLeft = 0.0f; // 冷却剩余（秒）：>0 时 tryAttack 返回 false
    float m_swingSeconds = kDefaultSwingTime;   // 扫程总时长（秒）：onEnter 时由配置覆盖（attack_swing_time）
    float m_sweepArcDegrees = kDefaultSweepArcDegrees; // 扫角（度）：onEnter 时由配置覆盖（attack_arc）
    float m_swingLeft = 0.0f;          // 横扫表现剩余时长（秒）：>0 时 render 绘制挥砍画面
    float m_swingDirX = 1.0f;          // 本次挥砍朝向 x 分量（单位向量，朝鼠标；初始向右）
    float m_swingDirY = 0.0f;          // 本次挥砍朝向 y 分量（同上）

    // ---- 受击反馈运行时（批五A；reset 清零）----
    // 击退位移在 update 内「先推进、后衰减」：本步完整贡献衰减前的速度位移，
    // 计时归零的那一步仍带位移——总步数 = 0.18s x 120Hz ≈ 22 步（报告口径）
    float m_knockbackVX = 0.0f;   // 击退速度 x（像素/秒；受击瞬间 = 方向 x 初速 480）
    float m_knockbackVY = 0.0f;   // 击退速度 y（同上）
    float m_knockbackLeft = 0.0f; // 击退剩余时长（秒；>0 = 击退窗口，输入被抑制）
    float m_hitTintLeft = 0.0f;   // 红染剩余时长（秒；>0 时 render 叠加红光）

    // ---- 落水表现（批六乙；纯渲染层，reset 清零）----
    // 下沉：绘制位置在逻辑位置上再向下偏移（玩家看似没入水中）；
    // 淡出：纹理 alpha 从 255 递减到 0。碰撞/位置/判定完全不受影响
    float m_drownSinkY = 0.0f;    // 下沉偏移（像素；0 = 无）
    Uint8 m_drownAlpha = 255;     // 本体不透明度（255 = 正常；0 = 完全淡出）

    // ---- 冻结锁定（批六丁·冻结条满 100；场景 setFrozen 控制）----
    bool m_frozen = false;        // true = 不可移动/不可攻击 + 裹冰视觉

    SDL_Texture* m_weaponTexture = nullptr; // 武器纹理（assets/weapon.png；加载失败为 nullptr，横扫跳过）

    // 行走精灵表的帧序约定（与 tools/compose_sprites.ps1 的组装顺序严格一致）
    static constexpr int kFrameCount = 5; // 精灵表总帧数（5 帧 x 48px = 240px 宽）
    static constexpr int kRunFirst = 0;   // 跑动子区间起点帧（移动时循环播放）
    static constexpr int kRunLast = 3;    // 跑动子区间终点帧（含）
    static constexpr int kIdleFrame = 4;  // 站立帧：静止时经 setAnimationRange 定格在这一帧
};

#endif // !__PLAYER_H__
