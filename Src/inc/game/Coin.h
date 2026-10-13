#ifndef __COIN_H__
#define __COIN_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

// ============================================================
// Coin：可被玩家收集的金币实体（带旋转帧动画）
// ============================================================
//
// 继承自 Entity 获得的能力（与 Player 共用，不再各自复制）：
//   - 位置/尺寸数据与 rect()、setPosition()、width()/height() 查询
//   - 纹理生命周期：release() 幂等释放，析构函数兜底
//   - 三级回退纹理加载：精灵表 -> 单帧图 -> 程序化纯色（loadTextureWithFallback）
//   - update()：基类默认实现「无条件推进帧动画」恰是金币需要的持续旋转
//   - render()：按当前帧从精灵表裁帧绘制（本类无需覆写）
//
// 本类自己的规则：
//   1. 纹理来源（三级回退，逐级降级的细节见基类）：
//      首选 assets/coin_sheet.png 旋转精灵表（6 帧横向排列），持续播动画；
//      次选 assets/coin.png 单帧静态精灵（旧版素材，动画退化为静止）；
//      回退为程序化 24x24 黄色方块（素材缺失时兜底，保证游戏可运行）
//   2. 动画节奏：6 帧循环、每帧 0.12 秒（约 8.3 帧/秒），与渲染帧率解耦
//   3. 生命周期：load() 创建纹理 ——> update() 推进动画（继承基类默认）
//      ——> render()/rect() 使用 ——> release() 释放（shutdown 与析构都会兜底）
// ============================================================

class Coin : public Entity
{
public:
    // 构造：固定金币尺寸为 24x24（正方形，也是精灵表的单帧尺寸）
    Coin()
    {
        m_w = kSize; // 宽 = 单帧宽
        m_h = kSize; // 高 = 单帧高（正方形金币）
    }

    // 加载金币纹理：只声明素材路径、帧参数与回退颜色，
    // 三级回退（精灵表 -> 单帧图 -> 程序化纯色）流程全部收敛在基类
    bool load(SDL_Renderer* renderer);

    // 在 (winW, winH) 窗口范围内随机重生，尽量避开 avoidRect（通常传玩家矩形）。
    // 这是金币特有的玩法逻辑，保留在本类；rect() 直接继承基类实现
    void respawn(int winW, int winH, const SDL_FRect& avoidRect);

private:
    static constexpr float kSize = 24.0f;        // 金币边长（正方形），也是单帧宽高
    static constexpr float kSpawnMargin = 40.0f; // 翻新位置与窗口边缘保留的边距
    static constexpr int kMaxTries = 20;         // 重生时「避开玩家」的最大重试次数

    static constexpr int kFrameCount = 6;      // 旋转精灵表总帧数（6 帧 x 24px = 144px 宽）
    static constexpr float kFrameTime = 0.12f; // 每帧显示时长（秒），约 8.3 帧/秒
};

#endif // !__COIN_H__
