#ifndef __XPGEM_H__
#define __XPGEM_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// ============================================================
// XpGem：经验宝石实体（怪物掉落、玩家拾取、旋转帧动画）
// ============================================================
//
// 名称说明：本类由既有 Coin 演进更名而来——收集物的语义从「金币计分」
// 改为「经验宝石」（升级系统的经验来源）。批三起金币回收作为独立实体
// （game/Coin.h）回归：击杀同时掉宝石与金币，两者同源帧、不同配色——
//   - 经验宝石 = coin_anim 源帧经 R<->B 换道染成青蓝调（蓝边/青身）；
//   - 金币 = coin_anim 原色（橙边/金身）。
// 染色在组装期完成（tools/compose_sprites.ps1 的 Convert-SwapRedBlue）：
// 乘法颜色调制无法把金色推成青蓝（蓝通道受源色彩限制），通道互换是
// 像素级重排、一次到位、运行期零开销。玩法差异：
//   - 不再随机重生：由怪物死亡时掉落在其位置（掉落动作由 PlayScene 完成）；
//   - 玩家碰到即拾取：场景累计经验并移除本对象，不做分数/连击判定。
//
// 继承自 Entity 获得的能力（与 Player/Monster/Coin 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - update()：基类默认实现「无条件推进帧动画」恰是宝石需要的持续旋转
//   - render()：按当前帧从精灵表裁帧绘制（本类无需覆写）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/gem_sheet.png 旋转精灵表（coin_anim 4 帧，每帧 18x21，
//      已染青蓝调）；次选 assets/gem.png 单帧静态精灵（同样染色）；
//      回退为程序化 18x21 青蓝色方块（素材缺失时兜底，与染色观感一致）
//   2. 动画节奏：4 帧循环、默认每帧 0.18 秒（代码常量，不进配置文件）——
//      原 6 帧 x 0.12 秒一个整转约 0.72 秒，4 帧时代取 0.18 秒保持
//      同一整转观感（0.18 x 4 = 0.72 秒）
//   3. 生命周期（批四·自动吸收）：怪物死亡时场景创建并 load -> 注入
//      吸收目标后每步朝玩家追踪飞行（homing）-> 与玩家碰撞盒重叠即
//      markPicked（场景 +10 经验）-> 场景统一清理（cleanup 时析构并
//      释放纹理）。玩家无需走动——掉落即飞向玩家
//   4. 追踪飞行（批四）：速度起步 kHomingSpeedStart、按 kHomingAccel
//      加速、封顶 kHomingSpeedCap（700 -> 900 像素/秒）；**飞行忽略
//      墙体**——吸收是奖励结算的视觉化（宝石不再是场上的物理实体，
//      穿墙可见即设计语义：让「到手」必有保障、绝不卡墙后捡不到）
// ============================================================

class XpGem : public Entity
{
public:
    // 默认帧时长：旋转动画每帧 0.18 秒（代码常量；4 帧整转约 0.72 秒）
    static constexpr float kDefaultFrameTime = 0.18f; // 默认每帧显示时长（秒）

    // 自动吸收的追踪速度曲线（批四；代码常量，规格 700~900px/s 带宽内）：
    // 起步 700、每秒加速 800、封顶 900（加速到顶约 0.25 秒）
    static constexpr float kHomingSpeedStart = 700.0f; // 起步速度（像素/秒）
    static constexpr float kHomingAccel = 800.0f;      // 加速度（像素/秒²）
    static constexpr float kHomingSpeedCap = 900.0f;   // 速度封顶（像素/秒）

    // 构造：固定宝石尺寸为 18x21（与精灵表的单帧尺寸一致）
    XpGem()
    {
        m_w = kWidth;  // 宽 18 像素：金币帧宽 6px x 3 倍放大（见 tools/compose_sprites.ps1）
        m_h = kHeight; // 高 21 像素：金币帧高 7px x 3 倍放大
    }

    // 加载宝石纹理：只声明素材路径、帧参数与回退颜色，
    // 三级回退（精灵表 -> 单帧图 -> 程序化纯色）流程全部收敛在基类
    bool load(SDL_Renderer* renderer);

    // 标记为「已被拾取」：场景在碰撞结算后调用，随后的统一清理会移除本对象。
    // 用「标记 + 延迟清理」而不是结算当场析构：避免在遍历集合的过程中
    // 删除元素造成迭代器失效
    void markPicked() { m_picked = true; }

    // 是否已被拾取（场景清理阶段据此移除对象）
    bool isPicked() const { return m_picked; }

    // 注入吸收目标（玩家碰撞盒中心的世界坐标；批四）：场景每固定步调用
    // ——宝石自掉落起持续朝玩家飞（自动吸收，玩家无需走动）；
    // 从未注入过时只播旋转动画、不位移（防御路径，不崩不飞）
    void setHomingTarget(float worldX, float worldY)
    {
        m_homingX = worldX; // 目标点世界坐标 x（本步快照）
        m_homingY = worldY; // 目标点世界坐标 y
        m_hasHoming = true; // 标记：此后 update 追踪它
    }

    // 覆写更新：基类帧动画之外，加入「自动吸收」的追踪位移（批四）——
    // 速度按 kHomingSpeedStart/Accel/Cap 推进；位移经 advanceHomingPoint
    // 纯函数（抵达不过冲，见 Homing.h）；飞行忽略墙体（吸收效果，见
    // 类头注释第 4 条）。拾取（抵达）由场景按玩家盒重叠判定后标记
    void update(const UpdateContext& ctx) override;

private:
    static constexpr float kWidth = 18.0f;  // 宝石宽（像素）＝单帧宽
    static constexpr float kHeight = 21.0f; // 宝石高（像素）＝单帧高
    static constexpr int kFrameCount = 4;   // 旋转精灵表总帧数（4 帧 x 18px = 72px 宽）

    bool m_picked = false; // 是否已被玩家拾取（拾取后等待场景清理）

    // ---- 自动吸收运行时（批四）----
    float m_homingSpeed = kHomingSpeedStart; // 当前追踪速度（像素/秒；加速到封顶为止）
    float m_homingX = 0.0f;   // 吸收目标 x（世界坐标；setHomingTarget 写入）
    float m_homingY = 0.0f;   // 吸收目标 y
    bool m_hasHoming = false; // 是否已注入目标（false = 只播动画不位移——防御）
};

#endif // !__XPGEM_H__
