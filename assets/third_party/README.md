# 第三方素材：0x72 DungeonTilesetII（CC0）

- **来源**：GitHub 仓库 [0x72/DungeonTilesetII](https://github.com/0x72/DungeonTilesetII)
  （作者 **0x72**；本地入库版本为 **v1.7**，文件名为包内原始命名）
- **许可**：**CC0 1.0 Universal（公共领域献纳）**——许可全文见本目录
  `LICENSE.txt`。CC0 不要求署名、允许任意用途（含商用），本项目仍主动
  记录来源以示尊重；游戏内成品素材均由本目录原始帧程序化组装而来
- **用途**：本游戏全部像素素材（玩家骑士 / 三种怪物 / 经验宝石 / 武器 /
  地牢地板背景）的原始帧来源；旧自绘素材已随本批全部退役

## 目录内容

| 路径 | 说明 |
| --- | --- |
| `frames/` | **拆散的单帧 PNG**（每个动画帧一张图）——组装管线的唯一输入 |
| `0x72_DungeonTilesetII_v1.7.png` | 官方整张图集（tileset atlas，入库留档） |
| `atlas_floor-16x16.png` 等 | 官方按类别拆分的地图集（留档备份） |
| `tile_list_v1.7` | 官方图块清单（tile 名称与序号对照表） |
| `LICENSE.txt` | CC0 1.0 许可全文（合法来源凭证，勿删） |
| `README` | 素材包原始说明文件 |

## 帧 → 成品映射（由 `tools/compose_sprites.ps1` 组装，输出到 `assets/`）

| 成品素材 | 原始帧（frames/ 内） | 帧数 | 说明 |
| --- | --- | --- | --- |
| `player_sheet.png` | `knight_m_run_anim_f0~f3` + `knight_m_idle_anim_f0` | 5 | 跑动 4 帧 + 站立 1 帧（放最后，引擎动画子区间定格用） |
| `player.png` | `knight_m_idle_anim_f0` | 1 | 单帧回退 |
| `monster_slime_sheet.png` | `swampy_anim_f0~f3` | 4 | 史莱姆——包内无 slime 命名，取同族的绿色软泥怪 swampy |
| `monster_goblin_sheet.png` | `goblin_run_anim_f0~f3` | 4 | 哥布林（有 run 用 run） |
| `monster_skelet_sheet.png` | `skelet_run_anim_f0~f3` | 4 | 骷髅（有 run 用 run） |
| `gem_sheet.png` | `coin_anim_f0~f3` | 4 | 经验宝石（旋转金币动画） |
| `weapon.png` | `weapon_knight_sword` | 1 | 骑士佩剑（攻击挥动表现；包内实际文件名） |
| `background.png` | `floor_1`（主）+ `floor_2~floor_8`（少量变体） | — | 1280x720 地牢地板，固定种子随机掺砖 |

## 成品重建（幂等）

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/compose_sprites.ps1
```

成品素材（`assets/` 下的 PNG）任何时候都可以从本目录原始帧**全量重建**：
脚本按「整数倍放大 + 最近邻采样」组装并自检尺寸，重复运行结果完全一致
（背景变体砖用固定随机种子）。改素材先改脚本，不要手工修图。
