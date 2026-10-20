#ifndef __PLAYSCENE_H__
#define __PLAYSCENE_H__

#include "engine/Scene.h"       // Scene：场景基类
#include "engine/AudioSystem.h" // AudioSystem：音效系统（拾取/受击/结束/暂停/破纪录触发播放）
#include "engine/Camera.h"      // Camera：相机（平滑跟随 + 世界↔屏幕换算 + 震屏；批二/M3甲）
#include "engine/ParticleSystem.h" // ParticleSystem：固定池矩形粒子（受击/击杀/拾取/升级/法术火花；M3甲）
#include "engine/Font.h"        // Font：TTF 字体资源（HUD/叠层文字/漂浮文字，支持中文）
#include "game/AttackArc.h"     // AttackArc：横扫扇形判定（纯函数，tests/attack_arc_test.cpp 直测）
#include "game/World.h"         // World：世界（确定性生成 + 区块 + 格级碰撞；批二）
#include "game/Zone.h"          // Zone：世界分区（草地/雪山——区表/查询/移速乘区；批六甲）
#include "game/Player.h"        // Player：玩家实体（移动 + 主动攻击 + 武器横扫表现）
#include "game/Monster.h"       // Monster：追击怪物（三变体、对象池、批三 AI 生态）
#include "game/MonsterRecycle.h" // MonsterRecycle：远处休眠怪回收判据（批六补丁，纯逻辑）
#include "game/XpGem.h"         // XpGem：怪物掉落的经验宝石
#include "game/Coin.h"          // Coin：怪物掉落的金币（批三局内货币）
#include "game/LevelCurve.h"    // LevelCurve：升级经验曲线（纯逻辑，tests/xp_test.cpp 直测）
#include "game/FloatingText.h"  // FloatingText：漂浮文字（伤害飘字与升级飘字共用）
#include "game/LevelupSfx.h"    // LevelupSfx：升级音效（游戏层直读；引擎加 SoundId::LevelUp 前的过渡）
#include "game/UpgradeCard.h"   // UpgradeCard：九项强化表（批五B 起由商店购买；抽取/叠加数学/欠账队列）
#include "game/Shop.h"          // Shop：商店价格公式与可购判定（纯逻辑，tests/shop_test.cpp 直测）
#include "game/ShopDesc.h"      // ShopDesc：商店效果描述（静态效果 + 动态数值对比文案；批六甲）
#include "game/ShopLayout.h"    // ShopLayout：商店九宫格布局（渲染与鼠标命中共享，防坐标漂移）
#include "game/Spell.h"         // Spell：法术池（六法术表/逐层参数/三抽抽取；纯逻辑，tests/spell_test.cpp 直测）
#include "game/SaveData.h"      // SaveData / BestRecord：最佳战绩的读取、比较与写盘
#include "game/Shrine.h"        // Shrine：复活点表 / 最近激活点 / 交互范围（批六乙）
#include "game/RespawnRules.h"  // RespawnRules：升级历史栈与死亡惩罚数学（批六乙，纯逻辑）
#include "game/ElementStatus.h" // ElementStatus：减速计时与冻结条（批六丁，纯逻辑）
#include <array>                // std::array：本扫程命中记录（按怪物池槽位的定长布尔表）
#include <list>                 // std::list：经验宝石与漂浮文字集合（节点式容器，迭代稳定）
#include <string>               // std::string：激活键提示文案（config 的 bind_activate 键名）
#include <vector>               // std::vector：怪物对象池 / 升级历史栈

// ============================================================
// PlayScene：对局场景（Roguelike 战斗核心）
// ============================================================
//
// 职责：
//   1. 资源：onEnter 生成世界（按 world_chunks_x/y/seed）、加载世界素材、
//      玩家/怪物纹理与字体（实体纹理各自析构兜底，玩家的武器素材由
//      Player 自持，世界纹理由 World 自持并在析构时释放；背景图自批二起
//      不再用于对局——区块地板取代，MenuScene 仍继续使用 background.png）
//   2. 世界与相机（批二核心；批六甲·世界重构）：
//      - 世界 12288x6144（8x4 区块；区块 1536px = 32 格 x 48px；批六甲），
//        分两个风格区（西 4x4 草地 / 东 4x4 雪山——game/Zone.h 区表），确定性
//        生成（坐标哈希 + 种子；seed=0 时每局随机）；世界最外 2 圈为
//        海洋水域（不可进入；「入水」归批六乙）+ 每区块随机墙簇（区块
//        交界与出生保护区保证可通行）；
//      - 分区表现（批六甲）：玩家中心所属区变化时弹「区名横幅」（大字
//        浮现约 1.5s）+ 打「进入区域：xxx」日志；雪山区玩家移速乘区
//        x0.85（Zone 表常量——applyCardEffects 与过区瞬间重注入）；
//        雪山区地砖/墙/装饰用冰霜重染色素材（World 烘焙按格所属区选表）；
//      - 玩家移动为「分轴位移 + 撞墙滑动」（World::slideMove，取代旧版
//        窗口 clamp）；怪物同样碰撞滑动 + 卡住时轻量侧滑（批三延续，
//        持久化侧滑改进见对应提交）；
//      - 相机每固定步 follow 玩家中心（指数平滑 camera_smooth_time）、
//        进局/重开 snapTo；暂停/结束冻结（相机不更新）；鼠标瞄准经
//        screenToWorld 换算后计算方向（零向量回退保留）；
//      - 震屏（M3·甲）：玩家受击 6px/0.15s、击杀 2px/0.1s、升级 3px/0.12s；
//        只扰渲染（worldToScreen），鼠标世界瞄准稳定；R 重开/暂停清抖动；
//      - 区块激活窗口（相机所在区块 ±1）内才更新/渲染怪物——非激活区
//        怪物暂停更新不删除；进入/离开窗口时烘焙/卸载区块纹理（日志
//        「区块 (x,y) 加载 / 卸载」供冒烟断言）；
//   3. 玩法（沿袭「鼠标瞄准·左键横扫」）：
//      - WASD 移动（朝向跟随鼠标——面向指针一侧，见 Player）；
//      - 主动攻击（本批核心，取代自动索敌）：鼠标左键按住 = 按冷却节奏
//        （attack_cooldown，默认 0.4s）连续横扫；单击挥一次；挥空也出招
//        （播放挥砍表现、不索敌、也不做命中结算）；
//      - 瞄准：攻击方向 = 玩家中心 -> 鼠标（零向量时回退到玩家面向）；
//      - 横扫（本批核心·刀锋同步命中）：扇形判定——以瞄准方向为中轴、
//        总张角 attack_arc（默认 120 度）、半径 attack_range（默认 96px）；
//        伤害不再出手瞬发全弧，而是随刀锋扫过逐帧结算：扫程
//        attack_swing_time 秒（默认 0.10）内，每个固定步取「上一步刀锋角
//        -> 本步刀锋角」的角区间，对扇形内、本扫程未命中、且角度落在
//        区间内的怪物此刻结算伤害 attack_damage（默认 25，随升级成长）、
//        红染、飘字与击退 attack_knockback（默认 30px）；多目标按刀锋
//        先后自然错开（不再同一瞬全结算），怪物移动中进入弧内会被扫到、
//        已命中不重复。判定与渲染共用 AttackArc.h 的缓出曲线纯函数
//        （bladeAngleAt / isAngleInSweep 等，单测覆盖边界）；
//      - 怪物三变体等权随机刷新（史莱姆 20/90、哥布林 30/110、骷髅 18/140
//        ——生命/基础速度，数值常量见 Monster.cpp 变体表）；批三正式版
//        出生规则；批六乙按甲批观察改为「玩家周围 520~900px 环带」采样
//        （要求 walkable、避水、激活区块内——恢复遭遇率，其余节奏不变）；
//        出生点即「巢」——怪物绕巢巡逻（monster_patrol
//        _radius）、玩家进入仇恨半径（monster_aggro_radius）才追击、
//        离巢超过栓绳半径（monster_leash_radius）回巢（AI 状态机见
//        MonsterAi.h，实体执行见 Monster；回巢回血 monster_return_heal
//        _percent 为预留键、默认关闭）；
//        刷新节奏与难度曲线沿用（数量上限初始 4、每 20s +2、封顶 12；
//        刷新间隔初始 2.5s、随局内时间收紧、下限 1s；首刷延迟 2s）；
//      - 怪物受击扣血 + 红染（批五A；旧白闪退役）+ 击退；死亡 +1 击杀
//        并原地掉落经验宝石与金币（批三：金币数按变体——史莱姆 1 /
//        哥布林 2 / 骷髅 1）；金币为「局内货币」：拾取计数（音效沿用
//        coin.wav）、本局累计、R 重开清零；本批只储备不消费（为将来
//        商店/强化体系铺路）；批四：金币磁吸——玩家进入
//        coin_magnet_radius（默认 140px）后朝玩家加速吸引、触及即拾取；
//        范围外静止不动（仍可走过去拾取）；
//      - 怪物攻击（批五A 升级·蓄力前扑强化版）：处于接触范围（双方碰撞盒
//        触接 + 6px 余量）且冷却就绪 -> 蓄力 0.35s（移速归零 + 后倾 8px +
//        头顶警示「!」——前扑开始消失；方向出招瞬间锁定 = 躲避窗口）->
//        前扑 24px / 0.12s（快出慢收曲线 + 1~2 重残影）-> 结算：玩家仍在
//        范围 = 命中——扣 monster_damage（默认 10）、玩家受击击退 + 0.8s
//        静默无敌、接触点爪痕特效、怪物回弹 40px + hit 音效；已离开 =
//        落空——原地收势 0.2s 再回追击；冷却键 monster_attack_cooldown
//        （默认 1.2s）；命中/落空/出招均有日志；
//      - 玩家 100 点生命（批五A 受击反馈改版）：被攻击命中扣血 + 受击击退
//        （沿「怪物 -> 玩家」方向推开：初速 480px/s 指数衰减、总位移约
//        42px、历时 0.18s；经碰撞滑动不穿墙；期间 WASD 输入被抑制）+
//        红染 0.3s（ADD 红光叠加）；0.8s 无敌保留但静默（防连续扣血——
//        旧版无敌闪烁已移除，渲染不再有任何跳隐）；
//      - 成长闭环（批四起：宝石自动吸收；M2 起：升级三选一；批五B：
//        选项改为法术）：
//        击杀掉落的经验宝石自动飞向玩家（homing：700~900px/s 加速、
//        穿墙可达；抵达即 +10 经验——玩家无需走动；日志「经验被吸收」；
//        M2 起经「慧眼」强化乘算提升——慧眼批五B 起由商店购买）-> 按
//        LevelCurve 曲线循环结算（溢出一枚可连升多级）-> 每级记一笔
//        「选择欠账」：进入 LevelUp 状态（世界冻结）弹出三张随机法术
//        （1/2/3 或鼠标点击卡片抉择——批五A；悬停高亮，三张不重复、
//        排除已满层），选择立即生效并记录「升级选择」日志；
//        欠账未清继续弹下一组（连升连选），清完回 Playing；
//        升级飘字/等级瞬闪/音效保留；M2 前的「每级自动强化」
//        （levelup_*）已退役——成长由法术（升级）+ 九项强化（商店）提供；
//        R 重开清零全部层数与效果（基准 + 层数重算、法术层数清零）
//      - 法术（批五B）：升级三选一改抽「法术池」（六种：火球术/冰霜新星/
//        闪电链/治疗之光/灼烧光环/护盾术；未学可学、已学叠层上限 5），
//        学会后自动施法（仅 Playing、各自冷却独立、无需按键、与横扫互不
//        打断）：火球向最近敌飞行+命中爆炸（伤害/频率/爆炸半径随层）；
//        冰霜新星以自身为心爆冰环（范围伤害 + 怪物速度乘区减速）；闪电链
//        劈击范围内随机敌并跳跃传导（跳数随层）；治疗之光周期回血（满血
//        不施放）；灼烧光环周身持续灼烧（每 0.5s 一跳，圈内无敌人不跳）；
//        护盾术周期获得护盾点数（受击先盾后血，HUD 显示护盾）。法术特效
//        （火球/冰环/闪电/治疗光/光环/护盾罩）为脚本生成贴图 + 代码动画
//        （飞旋/缩放/淡出/旋转）；HUD 右下角显示已学法术图标行与冷却条。
//        旧「升级发数值卡」退役——九项强化改在商店（B 键）用金币购买
//      - 商店（批五B）：对局中随时按 B 打开（世界冻结、同暂停语义），
//        九项强化（利刃/疾风/长臂/横扫/疾行/体魄/重击/慧眼/聚宝）只能用
//        金币购买；价格 = 基础价 x（层数 + 1）；面板 3x3 九宫格显示
//        「金币: N」与各项「层数/上限、价格」；悬停高亮、鼠标点击或
//        数字键 1~9 购买；金币不足/满层灰显 + 提示；B 或 ESC 关闭；
//        生效沿用 applyCardEffects 既有路径（与 M2 同一套加成代码）
//      - 生命归零与落水共用**死亡复活流**（批六乙，GameOver 界面退役）：
//        惩罚（金币 /2、等级 >1 时 -1 并丢失最近一次升级所得技能、该级
//        经验清零）-> 渐暗渐亮 0.6s -> 复活于距死亡地点最近的已激活神龛
//        -> 2s 无敌（静默、无闪烁）；死亡与 M 回菜单都会静默比较并写盘
//        最佳战绩（仅日志，不再弹「新纪录」界面）；
//      - 神龛（批六乙）：草地中心默认激活/发亮、雪山靠草地一侧未激活/
//        灰暗；走近约 64px 显示「按 E 激活」（bind_activate 可改键），
//        激活后发亮 + 音效 + 日志，本局保持；R 已无触发场景（绑定保留）；
//      - 海洋开放（批六乙）：玩家可进入水域（怪物仍视为实心）；玩家碰撞盒
//        与水格重叠 -> Drowning：输入锁定 + 水花 0.4s + 下沉淡出 0.5s
//        （期间无敌）-> 统一死亡处理；
//      - ESC 暂停/恢复、M 回主菜单（输入系统 pressed 边缘，可改键）；
//        落水/复活过场期间暂停与回菜单不响应（输入锁定）。
//   4. 音效：拾取播 coin、被撞播 hit、生命归零播 gameover、
//      ESC 暂停/恢复播 pause、破纪录播 record（经 onEnter 注入的 AudioSystem；
//      降级环境安全空操作）、升级播 levelup（LevelupSfx 游戏层直读，
//      见该类头文件说明——引擎 SoundId 扩展前的过渡实现）
//   5. 配置：onEnter 时读取玩家速度/生命/攻击参数（冷却/伤害/半径/张角/
//      击退/扫程时长 attack_swing_time）、怪物速度成长/刷新参数/攻击伤害
//      （monster_damage）与攻击冷却（monster_attack_cooldown）、
//      怪物生态（monster_patrol_radius 巡逻/monster_aggro_radius 仇恨/
//      monster_leash_radius 栓绳/回巢回血 monster_return_heal_percent）、
//      金币磁吸（coin_magnet_radius）、
//      世界（world_chunks_x/y、world_seed）与相机（camera_smooth_time）、
//      升级曲线（xp_level_base/xp_level_growth）——M2 起升级成长由三选一
//      卡牌提供（卡池/数值为代码常量，见 game/UpgradeCard.h；旧 levelup_*
//      自动强化键已退役删除）；
//      缺键/坏值自动回退内置默认值
//   6. 渲染：世界区块（地板/墙/装饰，经相机）-> 玩家（含横扫表现与受击
//      红染）-> 怪物（含前扑残影/蓄力警示/受击红染）-> 命中爪痕特效 ->
//      法术特效 -> 经验宝石 -> 金币 -> 粒子火花（M3甲：矩形粒子，随世界
//      一起震屏）-> 漂浮文字（以上全部世界坐标，绘制处经相机换算）
//      -> 生存 HUD（屏幕空间：左上 HP 血条+文字；其下等级（升级时瞬闪）/
//      经验条（当前级进度/所需）/击杀/存活/金币；左下角 FPS/帧时间，
//      F3 切换显隐）
//
// 「每次进入 = 全新一局」：每局都是新构造的 PlayScene 对象，
// 数据成员的默认值就是初始状态；onEnter 把玩家与怪物池布置到位。
// restart() 是完整复位入口：M3·甲起按 R（Playing/Paused）重新接回——
// 除既有战斗/世界/面板状态外，同时清空粒子与震屏；死亡自动复活流不变
//
// 状态机（PlayState）：
//   Playing  ：接受输入、推进战斗、检测碰撞与刷新、存活时间正计时
//   LevelUp  ：升级三选一面板（M2；批五A 支持鼠标）：世界冻结（同暂停
//              语义），响应 1/2/3 或鼠标点击卡片（悬停高亮；点击以左键
//              「按下边缘」为准，面板弹出时已按住左键需先松开再点——
//              防误触）与 F3（ESC/M/R 不响应——防误触）；选择生效，
//              欠账未清再弹下一组（连升连选），清完回 Playing
//   Shop     ：商店面板（批五B）：世界冻结（同暂停语义），随时可按 B 从
//              Playing 打开；鼠标悬停/点击与 1~9 购买九项强化（金币
//              消费，价格随层递增），B 或 ESC 关闭回 Playing（F3 照常；
//              R/M 不响应——防误触）
//   Paused   ：世界冻结（含存活计时与飘字动画），ESC 恢复、M 回主菜单
//               （M 退出前静默比较并写盘本局战绩；旧「中途退出不计」退役）
//   Drowning ：落水过场（批六乙）：输入锁定、水花 0.4s + 下沉淡出 0.5s、
//               期间无敌；计时结束进入统一死亡处理（不再有 GameOver 界面）
//   Reviving ：复活过场（批六乙）：渐暗渐亮共 0.6s；中点把玩家瞬移到
//               最近已激活神龛并 snapTo 相机；结束回 Playing + 2s 无敌
// ============================================================

// 对局状态机：用枚举而不是布尔标志，状态互斥关系一眼可见
enum class PlayState
{
    Playing,  // 正在游戏：接受输入、推进战斗、检测碰撞与刷新
    LevelUp,  // 升级三选一：世界冻结，仅响应 1/2/3（F3 走事件通道独立生效）
    Shop,     // 商店（批五B）：世界冻结，鼠标/1~9 购买，B/ESC 关闭（F3 照常）
    Paused,   // 暂停：世界冻结，ESC 恢复、M 回主菜单
    Drowning, // 落水过场（批六乙）：输入锁定、水花/下沉，结束后统一死亡
    Reviving  // 复活过场（批六乙）：渐暗渐亮、中点神龛复活，结束回 Playing
};

class PlayScene : public Scene
{
public:
    PlayScene() = default; // 成员默认值即初始局面，无需自定义构造

    // 析构：除成员的 RAII 释放（世界/实体纹理随各自析构）之外，本批（批五A）
    // 新增的攻击反馈纹理（alert/hit_fx）由本类自持——在此释放。时机安全：
    // SceneManager 先析构场景、后销毁 renderer，纹理先于 renderer 归还
    ~PlayScene() override;

    // 进入场景：保存 renderer 与音效系统、读取配置、生成世界并加载素材、
    // 加载字体与实体纹理、布置玩家与怪物对象池、初始化相机
    void onEnter(SDL_Renderer* renderer, const Config& config, AudioSystem& audio) override;

    // 事件：仅 F3 切换 FPS 显示（维持事件式调试开关）。
    // 暂停/重开/回菜单已迁移为 update 中的 pressed 动作边缘（见 handleInputEdges）
    void handleEvent(const SDL_Event& event) override;

    // 每帧更新：先处理输入动作边缘（按状态判据响应），再仅 Playing 状态
    // 推进战斗（Paused/LevelUp/Shop/Drowning/Reviving 冻结世界本体）。
    // 冻结时会把各实体的插值快照对齐到当前位置，保证画面完全静止不漂移；
    // 相机只在 Playing 分支推进（暂停/过场自动冻结——规格要求）
    void update(const UpdateContext& ctx) override;

    // 每帧渲染：清屏、世界区块（地板/墙/装饰，经相机）、玩家（含横扫表现
    // 与受击红染）、怪物（含残影/警示/红染）、命中特效、宝石、金币、漂浮
    // 文字、HUD（屏幕空间）。alpha 是插值系数，转发给实体；世界与实体的
    // 世界坐标都经场景持有的相机换算到屏幕
    void render(float alpha) override;

private:
    // 重开一局：生命/击杀/等级/经验复位、卡牌层数与欠账清零、三项属性回
    // 基准、玩家归位（世界中心）、回收全部怪物/宝石/飘字、刷新计时重排、
    // 清「新纪录」标记；相机 snapTo 玩家并立即刷新区块激活窗口
    //（新一局从干净的 3x3 开始）
    void restart();

    // 输入动作的按下边缘处理：暂停/恢复（Playing <-> Paused）、
    // 回菜单（Paused 下 M；退出前静默结算战绩）、神龛激活（Playing 下 E）。
    // pressed 在整帧的所有固定步都为 true，用「本帧已处理」标记去重，
    // 保证每次按下恰好触发一次（行为与事件式完全一致）。
    // 说明（M2/批六乙）：LevelUp/Shop/Drowning/Reviving 下这些动作不响应
    //（防误触/输入锁定）；R（restart）M3·甲起重新启用：仅 Playing/Paused
    // 响应（重开一局并清空粒子/震屏——见 restart()）
    void handleInputEdges(const UpdateContext& ctx);

    // ---- 落水与死亡复活流（批六乙）----

    // 死亡原因：统一死亡处理只用它决定日志文案（惩罚/复活动线完全共用）
    enum class DeathCause
    {
        HpZero,  // 生命归零（怪物攻击结算后）
        Drowning // 落水（Drowning 过场结束）
    };

    // 进入落水状态（玩家碰撞盒首次与水格重叠时调用）：
    //   状态切 Drowning -> 清空提示/扫程 -> 记录水花位置 -> 置满「期间无敌」
    //   -> 重置玩家下沉表现 -> 快照防拉丝；实际计时推进在 updateDrowning
    void enterDrowning();

    // 落水过场推进（每固定步）：
    //   0~0.4s 水花（splashLeft 递减）；0.4~0.9s 下沉/淡出
    //   （Player::setDrownVisual）；结束调用 beginDeath(Drowning)
    void updateDrowning(const UpdateContext& ctx);

    // 统一死亡处理（HP 归零与落水共用入口）：
    //   日志「死亡：生命归零/落水」-> 死亡音 -> 静默结算战绩（比较写盘）
    //   -> 惩罚（金币减半 / 等级 -1 / 升级历史栈出栈 / 经验清零）
    //   -> 选最近已激活神龛 -> 切 Reviving（渐暗渐亮 0.6s）
    // 防重：已处于 Reviving 时直接返回（同帧多次触发只处理一次）
    void beginDeath(DeathCause cause);

    // 复活过场推进（每固定步）：计时 + 中点（0.3s）瞬移复活 + 终点回
    // Playing 并给 2s 无敌（静默、无闪烁）
    void updateReviving(const UpdateContext& ctx);

    // 把玩家瞬移到指定神龛（复活中点调用）：中心对齐神龛、生命回满、
    // 相机 snapTo、立即刷新激活区块窗口、打「复活于（xxx）」日志
    void respawnPlayerAtShrine(int shrineIndex);

    // 打开（或再弹）升级三选一面板（M2；批五B：抽取对象改为法术池）：
    // 抽一组法术（三张不重复、排除已满层、池不足按实际）-> 有选项则进入
    // LevelUp 展示并打面板日志。池空（六法术全部 5 层满——理论晚局可达）
    // 时逐笔消费欠账并记录跳过，欠账清完后回到 Playing——「无选项可选」
    // 绝不卡死流程
    void openSpellPanel();

    // 升级选择输入处理（LevelUp 状态每固定步调用，M2；批五A 支持鼠标；
    // 批五B 选择对象改为法术）：输入两路并存——键盘 1/2/3 动作的按下
    // 边缘（去重标记与暂停同款）；鼠标左键「按下边缘」（pressed）命中
    // 某张卡。选中后 -> 法术层数 +1（初学冷却置 0 可立即施放）-> 打
    //「升级选择」日志 -> 消费一笔欠账 -> 未清再弹一组 / 清完回 Playing。
    // 防误触（规格）：点击以「按下边缘」为准；面板打开后的首个固定步若
    // 玩家已按着左键（战斗中连扫），则进入「等待松开」——须先观测到
    // released 一次、其后新的点击才生效（见实现内分步注释）。
    // 卡外点击（卡间隙/面板外）无效；按到空位（不足三张时按 3 等）时
    // 本步不做任何事，面板继续等待。悬停高亮（m_hoverCard）也在此刷新——
    // 与点击判定共用 CardLayout 的同一份布局计算
    void handleCardChoice(const UpdateContext& ctx);

    // ---- 商店（批五B：九强化迁移、金币购买）----

    // 打开商店（Playing 按 B 时由 handleInputEdges 调用）：切到 Shop 状态
    //（世界从下一固定步起冻结、同暂停语义）-> 清提示消息 -> 面板防误触
    // 基线置位（首个固定步采样左键按住状态）-> 打「商店开启」日志
    void openShop();

    // 关闭商店（B 或 ESC 触发）：切回 Playing（世界恢复推进）并打
    // 「商店关闭」日志。开/关互不冲突的编排见 handleInputEdges
    void closeShop();

    // 商店面板输入处理（Shop 状态每固定步调用，批五B）：悬停刷新（渲染
    // 高亮共用 ShopLayout）+ 1~9 数字键购买（动作 shop_1~9 的按下边缘，
    // 去重标记与暂停同款）+ 鼠标点击购买（左键按下边缘 + 命中 + 防误触
    // 解锁，与升级卡同款的「须先松开」口径）。命中项经
    // tryPurchaseShopItem 走购买流程；点击卡间隙/面板外无效（不消费）
    void handleShopInput(const UpdateContext& ctx);

    // 商店购买（批五B）：itemIndex 为强化表下标（0~8）。分支：
    //   - 满层 -> 提示「已达上限」+ 日志（不扣金币）；
    //   - 金币不足 -> 提示「金币不足」+ 日志（不扣金币）；
    //   - 可买 -> 扣金币（m_coinCount）-> 层数 +1（gainCardLevel 封顶）->
    //     applyCardEffects 统一重算生效 -> 体魄即时回血（与升级选择同款）
    //     -> 打「商店购买」日志（规格文案）-> 面板提示与购买音效
    void tryPurchaseShopItem(int itemIndex);

    // 强化效果应用（M2 升级卡；批五B 起是商店购买的生效入口；onEnter /
    // 升级选择生效 / 商店购买成交 / restart 四处调用的统一入口）：从
    // 「基准值 + 当前层数」重算全部受强化影响的数值并注入（幂等——重复
    // 调用结果一致；层数清零后调用即等价于「效果全部撤销」）。「即时回血」
    // 等一次性效果不在此列（选择/购买流程单独发放——重算路径绝不重复回血）。
    // 批六甲起移速注入含当前区乘区（雪地 x0.85）——见 currentZoneMoveMultiplier
    void applyCardEffects();

    // 当前区移速乘区（批六甲·双区）：ZoneId::Count（尚未判定——进局首步前）
    // 按草地（1.0）处理；过区瞬间 update 用新区的乘区重注入玩家移速
    float currentZoneMoveMultiplier() const;

    // 重算并注入玩家移速（批六丁）：最终速度 = 基础/卡牌移速 x 区乘区 x 减速乘区。
    // 减速生效/刷新/到期、过区、卡牌重算都经此唯一入口（乘算口径集中一处）
    void refreshPlayerMoveSpeed();

    // 怪物攻击命中后的元素结算（批六丁）：按攻击模组给玩家挂减速或累积冻结条。
    // 冻结满 100 时立即锁定玩家（不可移动/攻击）、取消横扫并进入 1.5s 冻结；
    // 冻结结束由 update 的 FreezeMeter 推进恢复（抗冻缓冲 2s 内不再累积）
    void applyAttackElement(const Monster& monster);

    // ---- 法术（批五B：六法术、自动施法）----

    // 火球飞行物：直线飞行（发射瞬间锁定方向——目标是移动的，不追踪），
    // 命中任一怪物或寿命到期即爆炸；伤害/爆炸半径在发射时按层数快照
    struct Fireball
    {
        float x = 0.0f;           // 中心 x（世界坐标）
        float y = 0.0f;           // 中心 y（世界坐标）
        float dirX = 1.0f;        // 飞行方向 x（单位向量）
        float dirY = 0.0f;        // 飞行方向 y（单位向量）
        float speed = 0.0f;       // 飞行速度（像素/秒；常量 kSpellFireballSpeed）
        float lifeLeft = 0.0f;    // 剩余寿命（秒；到点自爆——不会无限飞行）
        float rotation = 0.0f;    // 渲染旋转角（度；飞旋动画相位）
        int damage = 0;           // 命中伤害（点；发射时按层数快照）
        float blastRadius = 0.0f; // 爆炸半径（像素；发射时按层数快照）
    };

    // 火球爆炸特效：火球贴图从 0.6 倍放大到目标直径 + 淡出（缩放动画）
    struct FireballBlastFx
    {
        float x = 0.0f;           // 爆炸中心 x（世界坐标）
        float y = 0.0f;           // 爆炸中心 y
        float radius = 0.0f;      // 爆炸半径（像素；绘制尺寸 = 2 x radius）
        float left = 0.0f;        // 剩余寿命（秒；置满后递减，归零移除）
    };

    // 冰环特效：冰环贴图从 0.5 倍展开到目标直径 + 淡出（缩放+淡出动画）
    struct FrostNovaFx
    {
        float x = 0.0f;           // 冰环中心 x（施放时的玩家中心）
        float y = 0.0f;           // 冰环中心 y
        float radius = 0.0f;      // 判定半径（像素；绘制尺寸 = 2 x radius x 进度缩放）
        float left = 0.0f;        // 剩余寿命（秒；置满后递减，归零移除）
    };

    // 闪电链特效：折线路径（玩家中心 -> 各跳目标中心）+ 端点电花；
    // 折线锯齿在渲染时按点序号确定性生成（不引随机——画面每帧一致）
    struct ChainBoltFx
    {
        SDL_FPoint points[8] = {}; // 路径点（1 起点 + 最多 6 跳 = 7 点；8 留余量）
        int count = 0;             // 有效点数（< 2 时不画线）
        float left = 0.0f;         // 剩余寿命（秒；置满后递减，归零移除）
    };

    // 治疗光特效：玩家头顶光柱上浮 + 淡出
    struct HealFx
    {
        float x = 0.0f;           // 起点 x（施放时的玩家中心）
        float y = 0.0f;           // 起点 y（施放时的玩家头顶）
        float left = 0.0f;        // 剩余寿命（秒；置满后递减，归零移除）
    };

    // 法术自动施法总入口（仅 Playing，每固定步一次，怪物更新之后调用——
    // 碰撞/范围判定用本步怪物的最新位置）：冷却递减 -> 火球推进与命中
    // 爆炸 -> 各法术按冷却独立尝试施放（无需按键；条件不满足时静默等待）
    // -> 灼烧光环 tick -> 特效计时递减与清理。与横扫互不打断：独立的
    // 索敌/结算/特效链，双方都不影响对方的节奏
    void updateSpells(const UpdateContext& ctx, const SDL_FRect& playerBox);

    // 最近敌人查询：返回怪物池下标（-1 = 半径内无「存活且激活」的敌人）。
    // 火球（发射目标）与闪电链（跳跃）共用；距离按矩形中心计算。
    // exclude 可空：非空时跳过 exclude[i] 为 true 的槽位（闪电链防回跳）
    int findNearestMonster(float centerX, float centerY, float radius,
                           const bool* exclude) const;

    // 法术伤害结算（法术侧的统一伤害入口）：扣血（红染内置于 takeDamage）
    // -> 伤害飘字（浅蓝——与横扫的白色飘字区分来源）-> 致死走
    // resolveMonsterDeath（击杀/掉落/日志与横扫同款）。法术伤害不带击退
    //（与横扫的微击退区分：法术是自动补充，控制权留给横扫）
    void damageMonsterBySpell(Monster& monster, int damage, float centerX, float centerY);

    // 公共死亡结算（横扫与法术共用）：击杀计数 + 经验宝石/金币掉落 +
    // 「击杀怪物」「金币掉落」两条冒烟日志——文案与批三/批四完全一致
    //（拆出的唯一动机是消除两处结算链的复制）
    void resolveMonsterDeath(Monster& monster, float centerX, float centerY);

    // 各法术施放（由 updateSpells 在「层数 > 0 且冷却就绪」时调用）：
    // 条件不满足（无目标/满血/满盾/圈内无敌人）时静默返回且不重置冷却
    //（条件满足后立即施放）；施放成功才重置冷却并打「施法：xxx」日志
    void castFireball(const SDL_FRect& playerBox);      // 火球术：向最近敌发射
    void castFrostNova(const SDL_FRect& playerBox);     // 冰霜新星：冰环伤害+减速
    void castChainLightning(const SDL_FRect& playerBox); // 闪电链：随机首击+跳跃
    void castHolyHeal();                                // 治疗之光：周期回血
    void castShield();                                  // 护盾术：周期获得护盾
    void tickBurnAura(const SDL_FRect& playerBox);      // 灼烧光环：每 0.5s 一跳

    // 火球爆炸结算（命中怪物或寿命到期时调用）：爆炸半径内「存活且激活」
    // 的怪物全额伤害 + 爆炸特效入列 + 「火球爆炸」日志
    void explodeFireball(const Fireball& fireball);

    // 法术世界层特效渲染（render 内调用；分三段以维持正确的图层顺序）：
    //   - renderBurnAuraFx：灼烧光环（玩家脚下、玩家绘制之前——地面层）；
    //   - renderShieldFx  ：护盾罩（玩家绘制之后——罩在身上）；
    //   - renderSpellFx   ：火球/爆炸/冰环/闪电/治疗（怪物绘制之后、飘字之前）
    // alpha 为渲染插值系数：光环/护盾的挂点用玩家插值中心（与玩家绘制同源）
    void renderBurnAuraFx(float alpha) const;
    void renderShieldFx(float alpha) const;
    void renderSpellFx() const;

    // 法术 HUD（renderHud 内调用）：右下角已学法术图标行 + 层数 + 冷却条
    //（冷却未满时图标压暗、图标下方进度条按剩余比例回缩）
    void renderSpellHud(float screenW, float screenH) const;

    // 释放法术素材纹理（6 张特效 + 6 张图标；析构兜底；幂等）
    void releaseSpellTextures();

    // 主动攻击入口（每个固定步调用一次）：「左键按住 -> 方向 -> 出手 -> 扫程推进」。
    // 「场景读输入/算方向、玩家管出手、场景做命中结算」的分工：
    //   1. 左键没按住：本步不尝试出手（主动攻击——不按不挥）；
    //   2. 瞄准方向 = 玩家中心 -> 鼠标（批二：鼠标屏幕坐标经相机
    //      screenToWorld 换算到世界坐标；零向量时回退到玩家面向，见实现）；
    //   3. 调 Player::tryAttack 出手（冷却门控在玩家侧；挥空也出招）；
    //      出手成功：开启新扫程（清命中记录、对齐刀锋起点角）并记出手日志；
    //   4. 无论本步是否出手，都调用 advanceSwingSweep 推进已开启的扫程
    //      （出了手的刀必须扫完——表现与判定同生命周期）
    void resolvePlayerAttack(const UpdateContext& ctx, const SDL_FRect& playerRect);

    // 扫程推进（每个固定步调用）：刀锋同步命中的核心。
    // 扫程活跃期间，取「上一步刀锋角 -> 本步刀锋角」的扫过区间，对每只
    // 存活且本扫程未命中的怪物：中心在扇形内（距离/中轴夹角）且角度落在
    // 本步扫过区间内 -> 此刻结算（伤害/飘字/红染/击退/死亡掉落）；收尾步
    // 补扫到终点角并清理命中记录（「挥空」日志也在此刻给出）。
    // 刀锋角与进度来自 Player 的同一缓存与 AttackArc.h 共享纯函数——
    // 「画到哪、打到哪」由结构性共享保证
    void advanceSwingSweep(const SDL_FRect& playerRect);

    // 掉落一颗经验宝石到 (x, y)（通常为怪物中心）：创建对象、加载纹理、落位，
    // 并写入本颗宝石的经验值（xpValue——批六丙由怪物等级缩放后传入）
    void dropGemAt(float x, float y, int xpValue, float pickupDelaySeconds = 0.0f);

    // 掉落 count 枚金币到 (x, y) 周围（批三）：数量按变体（Monster::coinDrop()），
    // 多枚时绕掉落中心散开（避免完全重叠、一眼可见数量）。scatterRadius 为
    // 散开半径（像素；普通怪 12，Boss 用更大半径让 25~40 枚铺开可见）；
    // 落点做可行走校验（墙/水回退到中心），保证「掉落落在可行走地面」
    void dropCoinsAt(float x, float y, int count, float scatterRadius = kCoinDropScatter,
                     float pickupDelaySeconds = 0.0f);

    // ---- Boss（M3·乙）----
    // 场上存活 Boss 查询（同时最多 1 只）：non-const 供生成/日志，const 供 HUD
    Monster* findAliveBoss();
    const Monster* findAliveBoss() const;

    // 到点尝试降临（仅 Playing）：环带 500~700px 采样（激活区块 + walkable +
    // 避水），按落点所在区选型（草地=哥布林王 / 雪山=霜爵），等级固定
    // Lv20/Lv30；成功打「Boss 降临（名，等级 N）」日志。返回是否生成成功
    bool spawnBoss();

    // Boss 攻击命中/落空后的表现（重砸/冰环/冲锋）：粒子 + 震屏 6~8px。
    // 不改变伤害结算（无敌期/护盾/扣血仍由既有流程裁决），只补战斗反馈
    void spawnBossAttackFeedback(const Monster& monster);

    // 顶部 Boss 血条（屏幕顶部中央 480x18 + 名/Lv.n/当前/最大生命）：
    // 仅 Boss 存活时绘制；文字走 Font（名用 m_cardFont、数值用 m_smallFont）
    void renderBossHud();

    // 生成一处命中特效（批五A）：怪物攻击命中时由场景调用——(centerX,
    // centerY) 为接触点近似（双方中心的中点）；寿命置满（0.15s 渐隐），
    // 渲染在 render 的世界层绘制（见 m_monsterHitFxs 的说明）
    void spawnMonsterHitFx(float centerX, float centerY);

    // 释放攻击反馈纹理（批五A：alert/hit_fx）并置空（析构兜底；幂等）
    void releaseFeedbackTextures();

    // 释放批六乙事件纹理（shrine/splash）并置空（析构兜底；幂等）
    void releaseEventTextures();

    // 经验结算（拾取宝石时调用）：按升级曲线循环结算，并逐级发放强化与反馈
    // （生命上限+回血、攻击伤害、移速、日志、「升级！」飘字、HUD 瞬闪、音效）。
    // 曲线计算走 LevelCurve 纯函数——本函数只负责把结果落实到局面
    void grantXp(int amount);

    // 生成一条漂浮文字（伤害飘字/升级飘字共用）。同屏数量超过上限时
    // 先移除最老的一条（front），保证渲染开销有界、大量同屏不失控
    void spawnFloatingText(const char* text, float centerX, float topY, SDL_Color color,
                           float lifeSeconds = FloatingText::kDefaultLifeSeconds);

    // 最佳战绩结算（批六乙口径：死亡时 + M 回菜单时各调用一次）：
    // 比较三项纪录、更新内存快照，若有新纪录则立即写盘并打日志。
    // 「静默」= 不再播 record 音效、不弹「新纪录！」界面（GameOver 退役）；
    // 中途 M 退出同样参与比较写盘（旧「中途退出不计」口径退役）。
    // 注意：死亡路径在惩罚之前调用——纪录保留死亡瞬间已达成的等级/击杀/
    // 存活，而不是被惩罚后的等级（调平口径，见批六乙报告）
    void settleRecords();

    // 当前存活的怪物数（对象池中 alive 的槽位数；刷新上限的门控依据）
    int aliveMonsterCount() const;

    // 当前怪物数量上限：初始值（monster_max_count）+ 每 20s +2，封顶 12
    int currentMonsterCap() const;

    // 当前刷新间隔（秒）：初始值（monster_spawn_interval）随局内时间收紧，
    // 下限 1s
    float currentSpawnInterval() const;

    // 为指定空槽位寻找落点并生成怪物（批三出生规则 + 批六乙配平）：
    // 变体等权随机；落点采样改为「玩家周围 520~900px 环带」（均匀角度 +
    // 按面积均匀的半径），需同时满足：
    //   1. 处于当前激活区块窗口内（非激活区不生怪）；
    //   2. 怪物碰撞盒可走（isRectWalkable 含墙+水——不生进墙里/水里）；
    //   3. 圆心距落在 [520,900]（环带采样天然满足，防御校验保留）。
    // 落点重试上限内找不到（极少：环带被墙/水占满等）返回 false，本步不生；
    // 成功则 loadVariant（换皮）-> spawnAt（落位、出生点即巢）并返回 true
    bool spawnMonsterAtSlot(Monster& monster);

    // 远处休眠怪物回收（批六补丁·跨区槽位解锁）：在怪物更新与刷新之前
    // 每固定步扫描一次对象池（池 <= 12，开销可忽略）——对满足三条件的
    // 怪物执行 despawn 回池并打日志「回收远处怪物（变体，距玩家 N px）」：
    //   1. 所在区块非激活（休眠中，不在玩家眼前）；
    //   2. AI 处于巡逻（未仇恨、未回巢——追击/回巢中的怪不回收）；
    //   3. 距玩家中心严格大于 1800px（相机 3x3 窗口之外）。
    // 玩家跨区后的第一个固定步也走同一路径（本函数每步执行），旧区占满
    // 的 12 槽位会迅速释放，新区刷新门控「存活 < 上限」因此重新通过。
    // 判据抽为纯函数 shouldRecycleDormantMonster（game/MonsterRecycle.h），
    // 场景只负责取三输入、执行回收与日志
    void recycleDormantMonsters(float playerCenterX, float playerCenterY);

    // 把与玩家碰撞盒重叠的怪物按最小分离量推出（批四·体积碰撞兜底）：
    // 覆盖正常热更顺序之外的所有重叠来源（击退/瞬移落点、上一帧遗留）；
    // 玩家优先不动——被推的永远是怪物（推不动就原地等下一帧）
    void pushMonstersOutOfPlayer(const SDL_FRect& playerBox);

    // 怪物↔怪物轻量软分离（批四）：两两检查存活怪物的碰撞盒，重叠的按
    // 最小分离量各退一半（各自经 nudge 做墙/玩家校验；被挡的一侧少退不退）
    void separateOverlappingMonsters();

    // 绘制 HUD 文字/条（生存 HUD：HP 条、等级+经验条、击杀/存活；
    // FPS 调试行；暂停/升级/商店叠层；神龛激活提示）
    void renderHud();

    // 绘制两座神龛（世界层；按激活状态选「暗/亮」帧——批六乙）。
    // 只画与相机视口相交的项；素材缺失时静默跳过（纯视觉，不影响玩法）
    void renderShrines() const;

    // 绘制怪物等级小字「Lv.n」（批六丙；屏幕空间，血条旁）。
    // 位置与血条同锚点（插值头顶 + 同样的间隙/高度常量）；血条满血隐藏时
    // 等级仍显示——玩家在交手前就能读出威胁等级。字体复用 m_smallFont
    void renderMonsterLevel(const Monster& monster, float alpha) const;

    // 绘制水花特效（世界层；Drowning 前 0.4s——缩放 + 淡出）
    void renderSplashFx() const;

    // 绘制复活渐暗渐亮遮罩（Reviving 期间；0.6s 内 alpha 0->1->0）
    void renderReviveFade() const;

    // 采样本渲染帧的间隔时间并做 EMA 平滑（render 每帧开头调用）。
    // FPS 反映的是渲染性能，与固定步长逻辑无关，暂停时也照常采样
    void sampleFrameTime();

private:
    // ---- 配置项缺省值（与 config/game.ini 对应键一致；缺键/坏值回退）----
    static constexpr int kDefaultPlayerHp = 100;                // 玩家生命上限
    static constexpr int kDefaultAttackDamage = 25;             // 横扫伤害（attack_damage，点）：弧内每只各扣
    static constexpr float kDefaultAttackRange = 96.0f;         // 横扫半径（attack_range，像素）
    static constexpr float kDefaultAttackArc = 120.0f;          // 横扫总张角（attack_arc，度）：中轴两侧各一半
    static constexpr float kDefaultAttackKnockback = 30.0f;     // 横扫命中击退（attack_knockback，像素）
    static constexpr float kDefaultMonsterSpeedGrowth = 2.5f;   // 怪物速度成长（像素/秒²，按局内秒数叠加）
    static constexpr float kDefaultMonsterSpawnInterval = 2.5f; // 刷新间隔初始值（秒）
    static constexpr int kDefaultMonsterMaxCount = 4;           // 数量上限初始值（只）
    static constexpr int kDefaultMonsterDamage = 10;            // 怪物攻击伤害（monster_damage，点；攻击命中时扣）
    static constexpr float kDefaultMonsterAttackCooldown = 1.2f; // 怪物攻击冷却缺省（monster_attack_cooldown，秒）
    static constexpr float kDefaultCoinMagnetRadius = 140.0f;    // 金币磁吸半径缺省（coin_magnet_radius，像素）
    static constexpr float kDefaultMonsterPatrolRadius = 96.0f; // 巡逻半径缺省（monster_patrol_radius，像素）
    static constexpr float kDefaultMonsterAggroRadius = 320.0f; // 仇恨半径缺省（monster_aggro_radius，像素）
    static constexpr float kDefaultMonsterLeashRadius = 500.0f; // 栓绳半径缺省（monster_leash_radius，像素）
    static constexpr float kDefaultMonsterReturnHealPercent = 0.0f; // 回巢回血缺省（monster_return_heal_percent，0=关闭）
    static constexpr float kDefaultBossIntervalSeconds = kBossIntervalDefault; // Boss 降临间隔缺省（boss_interval_seconds，180s）
    static constexpr float kDefaultBossKillHealPercent = kBossKillHealPercentDefault; // Boss 击杀回血缺省（boss_kill_heal_percent，30%）
    static constexpr int kDefaultXpLevelBase = 30;              // 升级曲线基数（xp_level_base）
    static constexpr float kDefaultXpLevelGrowth = 1.35f;       // 升级曲线倍率（xp_level_growth）
    // 说明（M2；批五B 修订）：每级自动强化的 kDefaultLevelup* 常量已退役
    // 删除——升级成长由三选一法术提供（法术表与数值见 game/Spell.h），
    // 九项强化由商店金币购买（表与价格见 game/UpgradeCard.h / Shop.h）
    static constexpr int kDefaultWorldChunksX = 8;              // 世界横向区块数缺省（world_chunks_x）：8x4 = 12288x6144（批六甲）
    static constexpr int kDefaultWorldChunksY = 4;              // 世界纵向区块数缺省（world_chunks_y）：西 4x4 草地 + 东 4x4 雪山
    static constexpr float kDefaultCameraSmoothTime = 0.12f;    // 相机平滑时间常数缺省值（camera_smooth_time，秒）

    // ---- 玩法常量（代码固定，不进配置；改动须连同注释一起审视）----
    // 怪物对象池槽位数 = 普通怪上限 12 + Boss 专用 1（M3·乙）：
    // 普通怪到上限时仍有空槽位接纳 Boss，Boss 存活期间普通怪因上限少刷 1 只
    static constexpr int kMonsterPoolSize = 13; // 12 普通怪 + 1 Boss（普通怪上限仍为 12）
    static constexpr float kFirstSpawnDelay = 2.0f;      // 首刷延迟（秒）：开局 2 秒后第 1 只出现
    static constexpr float kCapGrowthPeriod = 20.0f;     // 数量上限成长周期（秒）：每 20 秒 +2
    static constexpr int kCapGrowthStep = 2;             // 每次成长的增量（只）
    static constexpr int kMaxMonsterCap = 12;            // 数量上限封顶（只；与池大小一致）
    static constexpr float kSpawnIntervalFloor = 1.0f;   // 刷新间隔下限（秒）：收紧到此为止
    static constexpr float kSpawnIntervalDecay = 0.025f; // 间隔收紧速率（秒/秒）：默认约 60 秒从 2.5 收到 1.0

    // 怪物速度上限（像素/秒）：不超过玩家默认速度 300，保证玩家永远可以甩开
    // 追兵（「可逃」是难度曲线的设计红线——速度成长不能把玩家逼进死局）。
    // 三变体共用同一条成长曲线：基础速度（史莱姆 90 / 哥布林 110 / 骷髅 140）
    // 之上每秒 +kDefaultMonsterSpeedGrowth，到达 260 后不再增长
    // （哥布林默认参数下约 60 秒到顶；骷髅更早、史莱姆稍晚）
    static constexpr float kMonsterMaxSpeed = 260.0f;

    // 玩家被攻击命中后的无敌时长（秒）：期间跳过怪物攻击的伤害结算。
    // 固定玩法规则（不进配置文件）：0.8 秒配合怪物命中回弹（40px）
    // 构成「受击保护窗口」。批五A：无敌期间不再有任何闪烁（静默保护）——
    // 受击的可读反馈由玩家的击退与红染承担（见 Player::applyHitKnockback）
    static constexpr float kPlayerInvincibleSeconds = 0.8f;

    // ---- 落水与死亡复活流（批六乙；代码常量，不进配置）----
    static constexpr float kDrownSplashSeconds = 0.4f; // 水花时长（秒；规格约 0.4）
    static constexpr float kDrownSinkSeconds = 0.5f;   // 下沉/淡出时长（秒；规格约 0.5）
    static constexpr float kDrownSinkPixels = 28.0f;   // 下沉总位移（像素；观感值）
    static constexpr float kDrownSplashSize = 64.0f;   // splash.png 绘制基准边长（像素）
    static constexpr float kReviveFadeSeconds = 0.6f;  // 渐暗渐亮合计时长（秒；规格）
    static constexpr float kReviveInvincibleSeconds = 2.0f; // 复活后无敌时长（秒；规格）
    static constexpr float kShrineDrawWidth = 64.0f;   // 神龛绘制宽（像素；素材每帧宽）
    static constexpr float kShrineDrawHeight = 80.0f;  // 神龛绘制高（像素；素材帧高）
    static constexpr float kShrineDrawLift = 64.0f;    // 神龛绘制的纵向抬升（像素；底部贴近落点）
    static constexpr float kShrinePromptYRatio = 0.82f; // 激活提示纵位置（屏高比例）

    // ---- 区名横幅（批六甲·双区世界）----
    // 玩家过区（含进局/重开后的首次判定）时区名大字浮现的时长与淡入淡出
    // 包络：总 1.5s = 淡入 0.3s + 保持 + 淡出 0.6s；透明度经渲染绘制色
    // 的 alpha 通道下发（Font 内部读取渲染器绘制色——批六甲实测可行）
    static constexpr float kZoneBannerSeconds = 1.5f;  // 横幅总时长（秒）
    static constexpr float kZoneBannerFadeIn = 0.30f;  // 淡入时长（秒）
    static constexpr float kZoneBannerFadeOut = 0.60f; // 淡出时长（秒）
    static constexpr float kZoneBannerTopRatio = 0.26f; // 横幅纵位置（屏高比例）

    // ---- 怪物命中特效（批五A）：接触点的爪痕/爆点 ----
    static constexpr float kMonsterHitFxSeconds = 0.15f; // 特效寿命（秒；规格约 0.15——渐隐至 0 即移除）
    static constexpr float kMonsterHitFxSize = 48.0f;    // 特效绘制边长（像素；与 monster_hit_fx.png 尺寸一致）

    // ---- 怪物出生（批三正式版：激活区块内随机 walkable 点；出生点即巢）----
    // 采样区域 = 相机激活区块窗口（最多 3x3 区块；玩家所在区块 ±1），
    // 在窗口矩形内均匀随机取点——「世界里的怪从世界里冒出来」，
    // 而不是围着玩家画环形。窗口已由 World 回钳在世界内，取点不会出界

    // 刷新环带（批六乙配平；像素）：落点距玩家中心 520~900。
    // 规格「玩家周围 520~900px 环带」——下界防贴脸、上界保证遇敌节奏；
    // 采样要求 walkable、避水、激活区块内（见 spawnMonsterAtSlot）
    static constexpr float kMonsterSpawnRingMin = 520.0f; // 环带内径（像素）
    static constexpr float kMonsterSpawnRingMax = 900.0f; // 环带外径（像素）

    // 找落点的重试上限（次）：环带采样可能撞墙/撞水/出激活窗口——
    // 上限内找不到就本步不生（下一刷新时刻再试），不会死循环。
    // 批六乙由 30 提到 64：环带面积比整窗口小，保持同样的生成成功率
    static constexpr int kMonsterSpawnAttempts = 64;

    // 瞄准零向量的判定阈值（像素²）：鼠标与玩家中心的距离比它更近时视为
    // 零向量（没有方向可言），攻击方向回退到玩家面向（见 resolvePlayerAttack）。
    // 1e-6 = (0.001 像素)²，远小于任何真实瞄准距离
    static constexpr float kAimEpsilonSquared = 1e-6f;

    // 每颗经验宝石的「基础」经验值（代码常量，不进配置）：10 点 = 默认曲线
    // 第 1 级需求（30）的三分之一——升到 2 级需要拾取 3 颗。
    // 批六丙起实际宝石经验 = round(kGemXpValue x (1 + 10% x (lv-1)))，
    // 按掉落源怪物等级缩放（纯函数见 game/MonsterLevel.h）
    static constexpr int kGemXpValue = 10;

    // ---- 怪物等级显示（批六丙）：血条旁「Lv.n」小字 ----
    // 与 Monster::kHpBarWidth/kHpBarHeight/kHpBarGapY 同锚点：文字画在
    // 血条右缘外 4px、底边与血条底边对齐（满血无血条时同一位置仍显示）
    static constexpr float kMonsterLevelTextGap = 4.0f; // 文字与血条右缘间隙（像素）
    static constexpr float kMonsterLevelTextYOffset = -1.0f; // 文字纵向微调（像素；视觉居中）

    // 金币掉落的散开半径（像素；批三）：同一次掉落的多枚金币绕中心
    // 均匀分布在这个半径上（2 枚时左右各一、更容易数清）；单枚直接落中心
    static constexpr float kCoinDropScatter = 12.0f;

    // 漂浮文字同屏上限（条）：超过上限时移除最老一条。数量级参考——默认
    // 节奏下命中冷却 0.4s、飘字寿命 0.8s，稳定同屏只有寥寥几条；上限是
    // 「横扫多目标同帧多飘字」这类大量同屏时的兜底，保证绘制开销有界不失控
    static constexpr int kMaxFloatingTexts = 64;

    // 升级时等级数字的高亮时长（秒）：窗口内数字颜色金白交替闪烁（瞬闪效果）
    static constexpr float kLevelFlashSeconds = 0.5f;

    // 升级高亮闪烁周期（秒）：金/白各占半周期（0.08s 切换一次，肉眼可感的短闪）
    static constexpr float kLevelFlashBlinkPeriod = 0.16f;

    // 「升级！」飘字寿命（秒）：比伤害数字更长——升级是稀有的高光反馈，
    // 多停留一会儿让玩家看清；伤害数字用 FloatingText 的默认寿命（0.8s）
    static constexpr float kLevelupTextLifeSeconds = 1.2f;

    static constexpr float kHudPtSize = 32.0f;  // HUD/提示文字字号（像素高，小数需求清晰）
    static constexpr float kBigPtSize = 64.0f;  // 暂停标题/区名横幅字号（大号观感）
    static constexpr float kCardPtSize = 24.0f; // 面板小字字号（升级卡描述/层数；M2/批五B）
    static constexpr float kSmallPtSize = 16.0f; // 商店效果描述小字（静态效果/数值对比/层数/价格；批六甲）

    // ---- 法术运行时常量（批五B；代码固定，改动须连同注释一起审视）----

    // 法术特效寿命（秒）：置满后固定步递减、归零移除；渲染按剩余比例渐隐
    static constexpr float kFireballBlastFxSeconds = 0.30f; // 火球爆炸（缩放+淡出）
    static constexpr float kFrostNovaFxSeconds = 0.35f;     // 冰环（展开+淡出）
    static constexpr float kChainBoltFxSeconds = 0.25f;     // 闪电链折线（淡出）
    static constexpr float kHealFxSeconds = 0.60f;          // 治疗光（上浮+淡出）

    // 火球弹体命中盒（像素）：以火球中心为中心的 24x24 小盒——与怪物脚盒
    // 做相交判定（稍微宽容的弹体体积，快速飞行不易穿漏）
    static constexpr float kFireballHitBoxSize = 24.0f;

    // 火球/冰环/治疗的绘制基准尺寸（像素；与脚本生成的素材尺寸一致）：
    // 火球 32、冰环 128、治疗 64、光环 160、护盾 96
    static constexpr float kFireballDrawSize = 32.0f;  // 火球贴图绘制边长（旋转绘制）
    static constexpr float kFrostNovaDrawSize = 128.0f; // 冰环贴图基准边长（再按半径缩放）
    static constexpr float kHealDrawSize = 64.0f;       // 治疗光绘制边长
    static constexpr float kBurnAuraDrawSize = 160.0f;  // 灼烧光环贴图基准边长（再按半径缩放）
    static constexpr float kShieldDrawSize = 96.0f;     // 护盾罩绘制边长（罩住 48x84 玩家）

    // 火球旋翼旋转速度（度/秒）：飞旋动画的相位推进速率
    static constexpr float kFireballSpinDegrees = 540.0f;

    // 治疗光上浮距离（像素）：寿命内从玩家头顶向上飘的总位移（淡出配合）
    static constexpr float kHealRisePixels = 30.0f;

    // 灼烧光环旋转速度（度/秒）与护盾呼吸频率/幅度：常驻表现的动画相位
    //（相位源 = 存活秒 m_survivalSeconds——暂停/商店冻结时画面完全静止）
    static constexpr float kBurnAuraSpinDegrees = 36.0f; // 光环自转角速度（度/秒）
    static constexpr float kShieldBreathHz = 1.0f;        // 护盾呼吸频率（次/秒）
    static constexpr float kShieldBreathScale = 0.04f;    // 护盾呼吸缩放幅度（±4%）

    // 闪电折线渲染参数：每段中点数与锯齿振幅（渲染时按点序号确定性生成）
    static constexpr int kChainBoltMidpoints = 4;         // 每段插入的中间点数
    static constexpr float kChainBoltJagPixels = 14.0f;   // 锯齿基础振幅（像素；按段长截断）

    // 法术 HUD 图标（右下角一行）：图标边长、间距与行底边距（像素）
    static constexpr float kSpellIconSize = 40.0f;        // 图标绘制边长（与素材一致）
    static constexpr float kSpellIconGap = 12.0f;         // 图标间距
    static constexpr float kSpellIconY = 64.0f;           // 图标行底边距（屏高 - 本值 = 行顶）
    static constexpr float kSpellIconMarginX = 20.0f;     // 行右缘边距（屏宽 - 本值 = 行右）

    // FPS 调试行专用：仍走 SDL 内置调试字体（8x8 点阵，仅 ASCII），
    // 缩放 2 倍 => 16 屏幕像素高，与改造前的调试行观感完全一致
    static constexpr float kFpsScale = 2.0f;

    SDL_Renderer* m_renderer = nullptr; // onEnter 保存的渲染器（只借用，不持有所有权）

    // onEnter 注入的音效系统指针（所有权在 GameDev，受管理期间必然非空）。
    // 对局用它在四处触发：拾取（update）、被撞（update）、结束（update）、
    // 暂停/恢复（handleInputEdges）
    AudioSystem* m_audio = nullptr;

    // 由 renderer 反查的窗口指针：相机视口（setViewport）需要窗口整数尺寸；
    // 窗口尺寸变化时每帧重新查询并喂给相机（与渲染基元无关的「逻辑尺寸」）
    SDL_Window* m_window = nullptr;

    // 世界（批二核心）：确定性生成（区块/墙/装饰）、格级碰撞查询、
    // 区块激活窗口的纹理烘焙与渲染。onEnter 生成一次；R 重开不重生成
    //（同一局同一世界——确定性冒烟「走远再回头内容一致」由此保证）
    World m_world;

    // 相机（批二核心）：每固定步 follow 玩家中心（camera_smooth_time 平滑）、
    // 进局/重开 snapTo；实体与区块渲染在绘制处经它换算到屏幕。
    // 暂停/结束不推进跟随（冻结）；震屏计时独立衰减（M3·甲），暂停时清抖动
    Camera m_camera;

    // 粒子系统（M3·甲）：固定池 512 粒，受击/击杀/拾取/升级/法术火花共用。
    // 只在 Playing 分支 update（与世界一起冻结：暂停/面板/过场全部静止）；
    // 渲染在世界实体之后、飘字之前（世界坐标经相机换算，随相机震屏抖动）
    ParticleSystem m_particles;

    // 字体资源：一个实例对应一个字号，加载失败时绘制安全跳过（Font 内部已警告）
    Font m_hudFont;  // HUD/提示文字（FPS 以外的文字、操作提示、结束列表）
    Font m_bigFont;  // 大号标题（暂停 / 区名横幅）
    Font m_cardFont; // 升级三选一面板的小字号文字（卡描述/层数标注；M2）
    Font m_smallFont; // 商店效果描述小字号（静态/对比/层数/价格；批六甲·第四笔）

    Player m_player;                 // 玩家实体
    std::vector<Monster> m_monsters; // 怪物对象池（预创建 kMonsterPoolSize 个槽位）

    // 怪物实心盒快照（批四·体积碰撞）：每步刷新为存活怪物的碰撞盒，
    // 注入玩家作为「不可穿越」的额外实心体。vector 常驻复用（clear +
    // push_back），capacity 稳定后无分配；只服务于单步内的指针注入
    std::vector<SDL_FRect> m_monsterSolidBoxes;
    std::list<XpGem> m_gems;         // 场上未被拾取的经验宝石
    std::list<Coin> m_coins;         // 场上未被拾取的金币（批三局内货币）
    std::list<FloatingText> m_floatingTexts; // 场上存活的漂浮文字（伤害/升级共用）

    // ---- 怪物命中特效（批五A）：接触点的爪痕/爆点 ----
    // 短暂存在（0.15s 渐隐）的世界层特效：vector 节点少、逐帧清理
    //（erase_if），与漂浮文字的容器选型差异无关紧要——两者都只在
    // 命中/受伤等低频事件时插入
    struct MonsterHitFx
    {
        float x = 0.0f;        // 特效中心世界坐标 x（接触点近似：双方中心的中点）
        float y = 0.0f;        // 特效中心世界坐标 y（同上）
        float left = 0.0f;     // 剩余寿命（秒）：降至 0 即移除（渲染按比例渐隐）
    };
    std::vector<MonsterHitFx> m_monsterHitFxs; // 场上命中特效（批五A）

    // ---- 攻击反馈纹理（批五A；场景自持，析构释放——renderer 尚在时安全）----
    // alert 一份共享给全部怪物槽位（setAlertTexture 注入；蓄力警示「!」）；
    // hit_fx 由场景在命中结算时自绘（接触点爪痕）。两者均为可选增强：
    // 加载失败只警告、对应表现跳过（游戏照常）
    SDL_Texture* m_alertTexture = nullptr;        // 蓄力警示「!」（alert.png）
    SDL_Texture* m_monsterHitFxTexture = nullptr; // 命中爪痕（monster_hit_fx.png）

    // 批六乙事件纹理（场景自持，析构释放——renderer 尚在时安全）：
    // shrine.png 两帧横向（暗/亮），splash.png 单帧；两者均为可选增强，
    // 加载失败只警告、对应表现跳过（玩法照常）
    SDL_Texture* m_shrineTexture = nullptr; // 神龛两态表（每帧 64x80；亮帧发亮）
    SDL_Texture* m_splashTexture = nullptr; // 落水水花（64x64；代码做缩放+淡出）

    // 升级音效播放器（游戏层直读 assets/sounds/levelup.wav）：
    // 引擎 AudioSystem 是固定 6 音效的强类型枚举、没有 LevelUp，而本批
    // 约束「引擎仓不动」——故用自包含小播放器过渡（设计见 LevelupSfx.h）。
    // 随场景构造/析构（RAII）：切场景或退出时自动关流释放
    LevelupSfx m_levelupSfx;

    // ---- 战斗配置（onEnter 读取；R 重开不重读，整局沿用同一套调参）----
    // 「基准值」= 配置里的初始值：卡牌效果从「基准 + 层数」重算（M2，
    // applyCardEffects 唯一入口）；R 重开清层数即回到基准。基准整局只读、从不被写
    int m_playerMaxHpBase = kDefaultPlayerHp;      // 生命上限基准（player_hp；体魄累加）
    int m_attackDamageBase = kDefaultAttackDamage; // 攻击伤害基准（attack_damage；利刃累加）
    float m_playerSpeedBase = Player::kDefaultSpeed; // 移速基准（player_speed；疾行累加）
    float m_attackCooldownBase = Player::kDefaultAttackCooldown; // 攻击冷却基准（attack_cooldown；疾风乘算）
    float m_attackRangeBase = kDefaultAttackRange; // 横扫半径基准（attack_range；长臂累加）
    float m_attackArcBase = kDefaultAttackArc;     // 横扫张角基准（attack_arc；横扫累加）
    float m_attackKnockbackBase = kDefaultAttackKnockback; // 击退基准（attack_knockback；重击比例）
    float m_coinMagnetRadiusBase = kDefaultCoinMagnetRadius; // 金币磁吸基准（coin_magnet_radius；聚宝累加）

    // 当前值：基准 + 卡牌层数重算（M2 起——applyCardEffects 统一注入；
    // restart 清层数后重算即复位）。它们才是实际参与战斗运算的数值
    int m_playerMaxHp = kDefaultPlayerHp;            // 当前生命上限（基准 + 体魄卡）
    int m_attackDamage = kDefaultAttackDamage;       // 当前攻击伤害（基准 + 利刃卡）
    float m_playerSpeed = Player::kDefaultSpeed;     // 当前移速（基准 + 疾行卡；经 setSpeed 注入）

    float m_attackRange = kDefaultAttackRange;        // 横扫半径（attack_range，像素）
    float m_attackArc = kDefaultAttackArc;            // 横扫总张角（attack_arc，度）
    float m_attackKnockback = kDefaultAttackKnockback; // 横扫命中击退（attack_knockback，像素）
    float m_monsterSpeedGrowth = kDefaultMonsterSpeedGrowth; // 速度成长（monster_speed_growth）
    float m_monsterSpawnInterval = kDefaultMonsterSpawnInterval; // 刷新间隔初始值（monster_spawn_interval）
    int m_monsterMaxCount = kDefaultMonsterMaxCount;      // 数量上限初始值（monster_max_count）
    int m_monsterDamage = kDefaultMonsterDamage;          // 怪物攻击伤害（monster_damage）
    float m_monsterAttackCooldown = kDefaultMonsterAttackCooldown; // 怪物攻击冷却（monster_attack_cooldown，秒）
    float m_coinMagnetRadius = kDefaultCoinMagnetRadius;  // 金币磁吸半径（coin_magnet_radius，像素）
    float m_bossInterval = kBossIntervalDefault;          // Boss 降临间隔（boss_interval_seconds，秒）
    float m_bossKillHealPercent = kBossKillHealPercentDefault; // Boss 击杀回血百分比（boss_kill_heal_percent，%）
    float m_bossNextAttemptTime = kBossIntervalDefault;   // 下一次 Boss 尝试时刻（局内秒；restart 重置）

    // 神龛激活键名（批六乙）：读取 config 的 bind_activate（默认 "E"），
    // 只用于提示文案「按 X 激活」——实际按键绑定由 GameDev 的输入系统完成。
    // 整局沿用（配置在启动时读取；R 重开不重读）
    std::string m_activateKeyLabel = "E";

    // 怪物生态参数（批三；AI 状态机的决策半径与回血——onEnter 读取后
    // 注入每个怪物槽位的 MonsterAi；整局沿用，R 重开不重读）
    float m_monsterPatrolRadius = kDefaultMonsterPatrolRadius; // 巡逻半径（monster_patrol_radius，像素）
    float m_monsterAggroRadius = kDefaultMonsterAggroRadius;   // 仇恨半径（monster_aggro_radius，像素）
    float m_monsterLeashRadius = kDefaultMonsterLeashRadius;   // 栓绳半径（monster_leash_radius，像素）
    float m_monsterReturnHealPercent = kDefaultMonsterReturnHealPercent; // 回巢回血百分比（0=关闭，预留键）

    // 相机平滑时间常数（秒；camera_smooth_time，默认 0.12）：
    // 越大越「拖」、越小越「贴」——onEnter 读取后整局沿用（restart 不重读）
    float m_cameraSmoothTime = kDefaultCameraSmoothTime;

    // ---- 升级配置与曲线（onEnter 读取；R 重开不重读）----
    // 批五B：升级成长由三选一法术提供（法术表与数值见 game/Spell.h），
    // 本节只保留经验曲线（旧 levelup_* 自动强化成员已退役删除）
    LevelCurve m_levelCurve; // 经验曲线（base/growth 从 xp_* 键注入）

    // ---- 战斗运行时状态（restart 复位）----
    int m_playerHp = kDefaultPlayerHp;   // 玩家当前生命（归零 -> 统一死亡复活流）
    int m_killCount = 0;                 // 本局累计击杀数（击杀日志/HUD/结算使用）
    int m_coinCount = 0;                 // 局内货币：本局累计金币数（批三掉落；批五B 起由商店消费）
    int m_level = 1;                     // 当前等级（restart 复位为 1；升级只增不减）
    int m_xp = 0;                        // 当前级已积累经验（结算后恒小于本级需求；HUD 进度条）
    float m_levelFlashLeft = 0.0f;       // 等级数字瞬闪剩余时长（秒；升级时置满，仅 Playing 递减）

    // ---- 升级与强化运行时状态（M2 起源；批五B 拆分服务）----
    // m_cardLevels 服务商店（九强化的层数/叠加数学/购买封顶）；欠账队列、
    // 随机源与面板卡位服务升级流程（三选一抽取的对象已改为法术池）
    int m_cardLevels[kCardCount] = {};       // 九项强化当前层数（下标与 CardId 一一对应；R 重开全清零）
    LevelUpQueue m_choiceQueue;              // 连升欠账队列（每次升级 +1 笔；选择消费；>0 保持逐次弹选）
    SpellDrawResult m_offeredSpells;         // 当前面板展示的法术（位序 1/2/3；count 为实际张数）
    CardSeedRandom m_cardRng;                // 抽取的种子化随机源（onEnter 播种；法术三抽共用——同种子可复现）
    bool m_choiceHandled[3] = { false, false, false }; // 1/2/3 按下边缘去重标记（与暂停同款语义）

    // ---- 商店运行时（批五B；restart 复位面板状态）----
    // 提示消息常驻（不自动淡出）：下一条操作覆盖、开/关面板清空——
    // 「冻结画面像素完全静止」的承诺因此不被消息动画破坏
    char m_shopMessage[80] = {};         // 面板底部提示（已购买/金币不足/已达上限）
    int m_hoverShopItem = -1;            // 悬停项下标（-1 = 无；Shop 每固定步刷新，渲染高亮消费）
    bool m_shopPanelFresh = true;        // 面板打开后的首个固定步标记（防误触基线采样；openShop 置位）
    bool m_shopClickNeedRelease = false; // 防误触：打开时按着左键 -> 须先观测到松开、新点击才生效
    bool m_shopClickHandled = false;     // 左键按下边缘去重标记（同帧多固定步只响应一次）
    bool m_shopKeyHandled[kShopItemCount] = {}; // 1~9 按下边缘去重标记（下标 = 商品序号）
    bool m_shopHandled = false;          // shop 动作本帧是否已处理（B 开/关边缘去重）

    // ---- 法术运行时（批五B；restart 复位）----
    // 层数/冷却为「已学法术」的全部运行时状态；飞行物与特效是短命实体
    //（逐帧清理，容量自然稳定）。素材纹理由本类自持（析构释放，幂等）
    int m_spellLevels[kSpellCount] = {};         // 各法术层数（下标与 SpellId 一一对应；R 重开清零）
    float m_spellCooldownLeft[kSpellCount] = {}; // 各法术剩余冷却（秒；独立计时、仅 Playing 递减）
    // 升级历史栈（批六乙）：每次三选一确认入栈（法术 + 选择后层数快照）；
    // 死亡且等级 > 1 时出栈最近一笔——该法术层数 -1，归零即停止施法/图标消失。
    // 出栈数学抽在 game/RespawnRules.h（纯函数，tests/respawn_test.cpp 直测）
    std::vector<UpgradeChoiceRecord> m_upgradeHistory;
    int m_playerShield = 0;                      // 当前护盾点数（先盾后血；HUD 显示；R 重开清零）
    std::vector<Fireball> m_fireballs;           // 飞行中的火球（命中/到点爆炸后移除）
    std::vector<FireballBlastFx> m_fireballBlasts; // 火球爆炸特效（0.3s 渐隐）
    std::vector<FrostNovaFx> m_frostNovas;       // 冰环特效（0.35s 展开淡出）
    std::vector<ChainBoltFx> m_chainBolts;       // 闪电链特效（0.25s 渐隐）
    std::vector<HealFx> m_healFxs;               // 治疗光特效（0.6s 上浮淡出）
    SDL_Texture* m_spellTextures[kSpellCount] = {};     // 六法术特效贴图（失败为 nullptr——表现跳过）
    SDL_Texture* m_spellIconTextures[kSpellCount] = {}; // HUD 图标（同上）

    // ---- 卡牌鼠标选择运行时（批五A；批五B 选择对象为法术）----
    int m_hoverCard = -1;                // 悬停卡位序（-1 = 无；LevelUp 每固定步刷新，渲染高亮消费）
    bool m_cardPanelFresh = true;        // 面板打开后的首个固定步标记（防误触基线采样；openSpellPanel 置位）
    bool m_cardClickNeedRelease = false; // 防误触：面板打开时按着左键 -> 须先观测到松开、新点击才生效
    bool m_cardClickHandled = false;     // 左键按下边缘去重标记（同帧多固定步只响应一次）

    // ---- 最佳战绩（onEnter 读档快照；restart 不重读——本局比较基准从头到尾一致）----
    BestRecord m_best; // 开赛时的历史最佳；死亡/M 退出结算后就地更新为最新纪录
                       //（批六乙起不再有 GameOver 界面与「新纪录」标记）

    // 本局存活时间（秒）：仅 Playing 正计时（暂停/面板/落水/复活过场冻结），
    // restart 清零。正向累积——死亡时参与最佳战绩比较（mm:ss 在 HUD 呈现）
    float m_survivalSeconds = 0.0f;

    // 玩家剩余无敌时间（秒；0 = 未处于无敌）；受击时重置为满值，
    // 仅 Playing 状态递减——暂停/过场时冻结（批五A：静默计时，
    // 不再有任何渲染表现；批六乙：复活完成后重置为 2s）
    float m_playerInvincibleLeft = 0.0f;

    // ---- 冰霜元素状态（批六丁；restart/死亡复活复位）----
    // 减速与冻结条都是纯逻辑小类（game/ElementStatus.h）：本场景负责
    // 计时推进、移速重注入、玩家锁定/HUD 显示与日志
    SlowStatus m_playerSlow;   // 冰史莱姆减速计时（x0.8 / 3s，刷新不叠乘）
    FreezeMeter m_freezeMeter; // 冻结条（0~100；满 100 冻结 1.5s；连续 3s 未受霜击后衰减 10/s；2s 缓冲）

    // ---- 分区运行时（批六甲；restart 复位）----
    // 当前所在区：ZoneId::Count = 尚未判定（进局/重开后首个 Playing 固定步
    // 会判定并触发「进入区域：草地」日志与横幅）；过区瞬间由 update 重注入
    // 玩家移速（×雪地 0.85；Zone 表常量）。横幅计时仅 Playing 递减——
    // 暂停/商店/升级面板下横幅随世界一起冻结
    ZoneId m_currentZone = ZoneId::Count; // 玩家中心当前所属区（未判定哨兵）
    float m_zoneBannerLeft = 0.0f;        // 区名横幅剩余时长（秒；0 = 不显示）

    // ---- 神龛运行时（批六乙；restart 复位）----
    bool m_shrineActive[kShrineCount] = { true, false }; // 各神龛激活状态（草地亮/雪山暗）
    int m_shrinePromptIndex = -1;   // 当前可激活神龛下标（-1 = 无提示）
    bool m_activateHandled = false; // 激活键按下边缘去重标记（同帧多固定步只响应一次）

    // ---- 落水与死亡复活运行时（批六乙；restart 复位）----
    float m_drownElapsed = 0.0f;      // 落水过场已进行时长（秒）
    float m_splashLeft = 0.0f;        // 水花剩余时长（秒；0 = 不画）
    SDL_FPoint m_splashPos{};         // 水花中心（世界坐标；进入落水时记录）
    float m_reviveTimer = 0.0f;       // 复活过场已进行时长（秒）
    bool m_reviveTeleported = false;  // 是否已到中点完成瞬移（防重复瞬移）
    int m_reviveShrineIndex = -1;     // 本次复活选中的神龛下标（死亡时确定）

    // ---- 怪物刷新运行时（restart 复位）----
    float m_monsterElapsed = 0.0f;       // 局内已过秒数（仅 Playing 递增；难度计时基准）
    float m_monsterNextSpawnTime = 0.0f; // 下一只怪物的刷新时刻（局内秒）
    int m_monsterSpawnedTotal = 0;       // 本局累计生成数（日志「第 N 只」/冒烟断言用）

    // ---- 刀锋同步命中运行时（本批新增；restart 复位）----
    bool m_sweepRunning = false;       // 扫程推进是否进行中（出手置 true、收尾置 false——收尾补段的判据）
    float m_sweepPrevAngleRad = 0.0f;  // 上一步的刀锋角（弧度）：本步扫过区间的起点；出手时对齐到起点角
    int m_sweepHitCount = 0;           // 本次扫程累计命中数（收尾判「这一刀是否命中过」——挥空日志依据）
    std::array<bool, kMonsterPoolSize> m_sweepHit{}; // 本扫程已命中记录（按怪物池槽位；新扫程开始/收尾均清零）

    PlayState m_state = PlayState::Playing; // 当前对局状态

    // ---- 输入动作边缘的去重标记 ----
    // 语义：记录「上一次固定步采样时该动作的 pressed 值」。
    // pressed 在整帧的所有固定步都为 true（引擎按渲染帧采样），若不做去重，
    // 一帧内补多个固定步会让暂停这类切换动作被响应多次；该标记把响应
    // 折叠为「每个上升沿恰好一次」。动作松开后标记随采样自然复位
    bool m_pauseHandled = false; // 暂停/恢复动作本帧是否已处理
    bool m_menuHandled = false;  // 回菜单动作本帧是否已处理
    bool m_restartHandled = false; // R 重开动作本帧是否已处理（M3·甲重新启用）

    // ---- FPS / 帧时间显示 ----
    // 显示内容：左下角 "FPS: 59.9 | 16.7 ms"。数值来自渲染帧间隔的
    // 指数滑动平均（EMA），只反映渲染表现，不参与任何玩法逻辑
    bool m_fpsVisible = true;      // F3 切换显隐；默认显示（学习项目便于观察）
    bool m_hasFrameSample = false; // 是否已采到首个有效帧间隔（首帧无间隔可测）
    float m_smoothedFrameMs = 0.0f;  // 帧间隔 EMA（毫秒）；FPS = 1000 / 该值

    // 上次 render 的时刻（性能计数器读数）：两次读数之差即渲染帧间隔。
    // 初值 0 表示「尚无参照」，第一次 render 只记录不计算
    Uint64 m_lastFrameCounter = 0;

    // 窗口实际尺寸缓存（每次查询窗口后更新）：相机 setViewport 与
    // 视口相关的整数换算直接用它们（自批二起出生/边界规则都归世界与相机）
    int m_width = 0;
    int m_height = 0;
};

#endif // !__PLAYSCENE_H__
