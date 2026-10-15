#ifndef __COIN_H__
#define __COIN_H__

#include "engine/Entity.h" // Entity：实体基类（空间/纹理/帧动画公共能力）

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
//   3. 生命周期：怪物死亡时场景创建并 load -> update 播动画 -> 玩家触碰后
//      markPicked -> 场景统一清理（cleanup 时析构并释放纹理）。
//      拾取音效沿用 coin.wav（与宝石拾取共用同一反馈，语义恰好吻合）
// ============================================================

class Coin : public Entity
{
public:
    // 默认帧时长：旋转动画每帧 0.18 秒（代码常量；4 帧整转约 0.72 秒）
    static constexpr float kDefaultFrameTime = 0.18f; // 默认每帧显示时长（秒）

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

private:
    static constexpr float kWidth = 18.0f;  // 金币宽（像素）＝单帧宽
    static constexpr float kHeight = 21.0f; // 金币高（像素）＝单帧高
    static constexpr int kFrameCount = 4;   // 旋转精灵表总帧数（4 帧 x 18px = 72px 宽）

    bool m_picked = false; // 是否已被玩家拾取（拾取后等待场景清理）
};

#endif // !__COIN_H__
