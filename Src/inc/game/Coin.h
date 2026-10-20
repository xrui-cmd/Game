#ifndef __COIN_H__
#define __COIN_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）
#include <algorithm>       // std::max：拾取延迟/磁吸参数的边界夹取

// ============================================================
// Coin：金币实体（怪物掉落、玩家拾取、旋转帧动画；批三新增）
// ============================================================
//
// 定位（批三·局内货币）：击杀怪物的掉落物之一（另一件是经验宝石），
// 玩家碰到即拾取——PlayScene 累计局内金币数（本批不消费，为将来
// 商店/强化体系做储备）。与 XpGem 是「同源不同色」的一对：
//
// 外观区分（本批的展示约定，「一眼可分」）：
//   - 金币 = 素材包金币原色（橙边/金身/白高光），素材 assets/coin_*.png；
//   - 经验宝石 = 同源帧经 R<->B 换道染成青蓝调（蓝边/青身/白高光）。
//   两者的染色链路都在 tools/compose_sprites.ps1（组装期一次完成——
//   乘法颜色调制无法把金色推成青蓝，通道互换是像素级重排的可靠做法；
//   运行期零开销，见脚本内 Convert-SwapRedBlue 注释）
//
// 继承自 Entity 获得的能力（与 Player/Monster/XpGem 共用）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - update()：基类默认实现「无条件推进帧动画」恰是金币需要的持续旋转
//   - render()：按当前帧从精灵表裁帧绘制（本类无需覆写）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级细节见基类）：
//      首选 assets/coin_sheet.png 旋转精灵表（coin_anim 4 帧，每帧 18x21）；
//      次选 assets/coin.png 单帧静态精灵（动画退化为静止）；
//      回退为程序化 18x21 金黄色方块（素材缺失时兜底，保证游戏可运行）
//   2. 动画节奏：4 帧循环、默认每帧 0.18 秒（代码常量，不进配置文件）——
//      与经验宝石同一节奏（同源帧素材，整转观感一致：0.18 x 4 = 0.72 秒）
//   3. 生命周期（批四·磁吸版）：怪物死亡时场景创建并 load -> 每步由
//      场景注入玩家盒中心；玩家中心进入 coin_magnet_radius 后开始磁吸
//      （朝玩家加速吸引、触及即拾取）-> markPicked -> 场景统一清理。
//      **范围外静止不动**（仍可像旧版一样走过去拾取）；磁吸一旦触发
//      即保持（跑出范围也不会半路丢下——「吸到手」的确定性）
//   4. 磁吸飞行（批四）：速度从 0 起步、按 kMagnetAccel 加速、封顶
//      kMagnetMaxSpeed（800 像素/秒）；**飞行忽略墙体**（磁吸是拾取的
//      强化表演：进入吸附半径即必有回报，不因地形打折）
// ============================================================

class Coin : public Entity
{
public:
    // 默认帧时长：旋转动画每帧 0.18 秒（代码常量；4 帧整转约 0.72 秒）
    static constexpr float kDefaultFrameTime = 0.18f; // 默认每帧显示时长（秒）

    // 磁吸速度曲线（批四；代码常量）：从静止加速（加速度 2000px/s²）、
    // 封顶 800px/s——在默认半径 140px 下约 0.37 秒吸到脚下
    static constexpr float kMagnetAccel = 2000.0f;    // 加速度（像素/秒²）
    static constexpr float kMagnetMaxSpeed = 800.0f;  // 速度封顶（像素/秒）

    // 构造：固定金币尺寸为 18x21（与精灵表的单帧尺寸一致）
    Coin()
    {
        m_w = kWidth;  // 宽 18 像素：金币帧宽 6px x 3 倍放大（见 tools/compose_sprites.ps1）
        m_h = kHeight; // 高 21 像素：金币帧高 7px x 3 倍放大
    }

    // 加载金币纹理：只声明素材路径、帧参数与回退颜色，
    // 三级回退（精灵表 -> 单帧图 -> 程序化纯色）流程全部收敛在基类
    bool load(SDL_Renderer* renderer);

    // 标记为「已被拾取」：场景在碰撞结算后调用，随后的统一清理会移除本对象。
    // 用「标记 + 延迟清理」而不是结算当场析构：避免在遍历集合的过程中
    // 删除元素造成迭代器失效（与 XpGem 同一纪律）
    void markPicked() { m_picked = true; }

    // 是否已被拾取（场景清理阶段据此移除对象）
    bool isPicked() const { return m_picked; }

    // ---- 磁吸（批四）----

    // 开始磁吸（场景在「玩家中心进入 coin_magnet_radius」时调用一次）：
    // 此后本枚金币每步朝玩家盒中心加速飞——一次性触发、永久保持
    void beginMagnet() { m_magnetized = true; }

    // 是否已磁化（场景查询用：未磁化才做半径判定；渲染/日志可扩展使用）
    bool magnetized() const { return m_magnetized; }

    // ---- 拾取延迟（M3·乙：Boss 掉落落点可见）----

    // 设置拾取延迟（秒）：延迟内不触发磁吸、也不可被拾取（仍播放旋转动画）。
    // 用途：Boss 死亡时 25~40 枚金币在脚下铺开，若立即磁吸会一瞬间吸光，
    // 玩家看不到「掉落」。普通怪掉落不调用（保持既有即时拾取手感）
    void setPickupDelay(float seconds)
    {
        m_pickupDelayLeft = std::max(0.0f, seconds); // 负值按 0（无延迟）
    }

    // 当前是否可被磁吸/拾取（拾取延迟已走完）
    bool canPickup() const { return m_pickupDelayLeft <= 0.0f; }

    // 拾取延迟剩余时长（秒；冒烟/调试查询）
    float pickupDelayLeft() const { return m_pickupDelayLeft; }

    // 注入磁吸目标（玩家碰撞盒中心的世界坐标；场景每固定步在磁化后调用）：
    // 目标随玩家移动刷新——金币追的是「当前的脚下」，不是触发时那一点
    void setMagnetTarget(float worldX, float worldY)
    {
        m_magnetX = worldX; // 目标点世界坐标 x（本步快照）
        m_magnetY = worldY; // 目标点世界坐标 y
        m_hasTarget = true; // 标记：此后 update 追踪它（磁化后才消费）
    }

    // 覆写更新：基类帧动画之外，加入磁吸追踪位移（批四）——速度按
    // kMagnetAccel/kMagnetMaxSpeed 推进；位移经 advanceHomingPoint 纯函数
    //（抵达不过冲，见 Homing.h）；未磁化/未注入目标时静止不动。
    // 拾取（触及）由场景按玩家盒重叠判定后标记
    void update(const UpdateContext& ctx) override;

private:
    static constexpr float kWidth = 18.0f;  // 金币宽（像素）＝单帧宽
    static constexpr float kHeight = 21.0f; // 金币高（像素）＝单帧高
    static constexpr int kFrameCount = 4;   // 旋转精灵表总帧数（4 帧 x 18px = 72px 宽）

    bool m_picked = false; // 是否已被玩家拾取（拾取后等待场景清理）

    // ---- 磁吸运行时（批四）----
    bool m_magnetized = false;   // 是否已触发磁吸（一次性、永久保持）
    float m_magnetSpeed = 0.0f;  // 当前磁吸速度（像素/秒；从静止加速到封顶）
    float m_magnetX = 0.0f;      // 磁吸目标 x（世界坐标；setMagnetTarget 写入）
    float m_magnetY = 0.0f;      // 磁吸目标 y
    bool m_hasTarget = false;    // 是否已注入目标（false = 不位移——防御）
    float m_pickupDelayLeft = 0.0f; // 拾取延迟剩余（秒；>0 时不可磁吸/拾取）
};

#endif // !__COIN_H__
