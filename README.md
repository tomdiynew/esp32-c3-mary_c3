# 圣母献花 esp32-c3-mary_c3

ESP32-C3 SuperMini + 1.50 寸 240×280 SPI 彩屏（GC9306 / GC9307）做的献花小程序，基于 **ESP-IDF v6.1**。
在圣母像前献花是天主教的敬礼传统（如五月圣母月）：按一下板载 BOOT 键，就献上一朵花；
献花数保存在 Flash 里。

![preview](art/preview.png)

预览图从左到右：开机画面 / 献花中（花飞入花瓶、右上角小百合）/ 进入祭台时的「今日祝福」。

## 玩法

| 画面 | 操作 | 效果 |
|---|---|---|
| 开机画面 | 单击 BOOT | 进入祭台，显示今日祝福 |
| 祭台 | 按下 BOOT | 献花一朵，按下即响应 |
| 祭台 | 长按 BOOT **1 秒** | 献花数清零，并从 Flash 删除，提示「献花数已清零」 |

- **开机**：logo 图铺满全屏，白色花瓣缓缓飘落，底部浅蓝缎带风格的「点击献花」按钮呼吸闪烁。
- **今日祝福**：进入祭台时，白色花瓣如雨飘落，神龛泛起金光，屏幕顶部落下一条浅蓝缎带，随机显示一句祝福
  （共 24 句，见 [`art/blessings.txt`](art/blessings.txt)），约 4.5 秒后连同花瓣一起淡出。
  缎带放在顶部，不遮挡圣母像。
- **祭台**：
  - 场景：金色拱形神龛里是圣母像（从 logo 图裁出），背后是蓝色彩窗和柔光，两侧烛光闪烁，祭台铺白色金边桌布，中间一只白瓷花瓶。
  - **献花**：每按一下，一朵百合或白玫瑰从下方沿弧线飞进花瓶，拖着星光。落下时星光四散、神龛泛起金光，
    右上角一朵小百合在原地浮现再淡出，顶部面板的「已献 N 朵」跳一下后加 1。
  - 前 20 朵依次插满花瓶，满了以后新献的花会飞入花束中随机一处。
  - 连按时每一下都会计数，最多同时 6 朵在空中。
- 献花数停止操作 1 秒后写入 Flash（NVS），断电重启后接着计数。数据只存在设备本地，不联网。

## 设计上的考虑

- **不做「刷分」**：不用木鱼式的「+1」上浮，改为原地淡入淡出的小百合；计数写作「已献 N 朵」。
- **祝福不作奖励**：祝福在进入祭台时作为「今日祝福」出现，不设「满 N 朵解锁」。
- **祝福语的措辞**：宗教性的句子用天主教用语——「降福」「天主」，对圣母用「为我们祈求」；
  「垂怜」在礼仪中只向天主祈求，所以不用于圣母。不写「心想事成」「所求皆如愿」「好运连连」这类
  暗示有求必应或讲运气的话。其余是平安、感恩一类的通用祝福。发布前建议请教友再看一遍。
- **不遮挡圣像**：所有浮层（缎带、提示）都避开神龛里的圣母像。
- 面向天主教用户；新教不敬礼玛利亚，所以文案里用「敬礼」「献花」，不写「崇拜」。
- 开机图为 AI 生成的艺术形象。

## 硬件与接线

与 [esp32-c3-muyu](https://github.com/tomdiynew/esp32-c3-muyu) 相同：ESP32-C3 SuperMini（BOOT 键 = GPIO9）+
FPC-1502401（GC9306）或 FPC-1502403（GC9307），开机自动识别。引脚在 [`main/board.h`](main/board.h)。

| 屏 FPC 引脚 | 信号 | ESP32-C3 |
|---|---|---|
| 1 | LCD_RES | GPIO4 |
| 2, 21 | GND | GND |
| 4 | LCD_CS | GPIO10 |
| 5 | LCD_SCL | GPIO6 |
| 7 | LCD_SDA | GPIO7 |
| 24 | LCD_DC | GPIO5 |
| 8, 15, 16 | LCD_3.3V | 3V3 |
| 11, 12 | LED_A | 3V3 经 10Ω |
| 13, 14 | LED_K | GND（或 N-MOSFET，栅极接 GPIO3） |

## 编译和烧录

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
git clone https://github.com/tomdiynew/esp32-c3-mary_c3.git
cd esp32-c3-mary_c3
idf.py set-target esp32c3        # 首次
idf.py build
idf.py -p COMx flash monitor
```

生成好的图片和字形都已入库，直接编译即可。

- 屏幕上的「点击献花」指按板载 **BOOT 键**（本项目没有接触摸）。
- 烧录不进时：按住 BOOT，点一下 RESET，松开 BOOT，再烧录。
- menuconfig 里 `Mary flowers → LCD panel` 可手动指定屏型号 / 反色，默认自动识别。
- 重新烧录不会清掉献花数（保存在 NVS 分区）；想从 0 开始，长按 BOOT 1 秒，或执行 `idf.py -p COMx erase-flash` 后再烧录。

## 资源占用

| | 大小 |
|---|---|
| 固件 | 约 0.72 MB（程序分区 3.9 MB，剩 82%） |
| 开机图 + 祭台背景 | 2 × 134 KB |
| 精灵图（百合 ×3、白玫瑰 ×2、满天星、花瓣 ×4、烛火、光晕、缎带、按钮等） | 约 165 KB |
| 祝福语楷体字形（70 字，带描边） | 约 45 KB |
| RAM | 静态约 60 KB + 送屏缓冲 2 × 19 KB |

## 美术与文字

- [`tools/make_art.py`](tools/make_art.py)：开机图缩放（源图 `art/logo_src.png`）；祭台背景分三层合成——
  教堂（彩窗、光束、烛光虚化），从源图裁出的圣母像贴进拱形神龛，前景（神龛金框、祭台、花瓶、蜡烛、计数面板）；
  花、花瓣、烛火、缎带等用 SVG 画，无头 Edge/Chrome 渲染成带 alpha 的精灵图。花束的 20 个位置也在这里定义。
- [`tools/make_blessings.py`](tools/make_blessings.py)：祝福语（今日祝福），生成 `main/blessings.h`（楷体带描边字形），
  同时输出 `art/blessings.txt`。
- 计数数字的字体（`main/font_ui_data.h`）由 `tools/make_ui_font.py` 生成，与电子木鱼相同。

修改后重新生成（需要 Windows + Python、Pillow，以及 Edge 或 Chrome）：

```powershell
python tools\make_art.py --preview art\preview.png
python tools\make_blessings.py
idf.py build
```

## 工程结构

```
main/
  main.c        开机 / 祭台两个画面、今日祝福（花瓣雨 + 缎带）、献花飞行与计数、NVS 保存 / 清零
  lcd.c         SPI + GC9306/9307 初始化、自动识别
  button.c      BOOT 键事件
  ui.c          抗锯齿数字
  board.h       引脚
  sprites.h     生成的精灵表、花束位置、布局常量
  blessings.h   生成的祝福语与字形
  art/          生成的图片资源
components/esp_lcd_gc9306/   屏驱动组件
tools/          资源生成脚本
art/            logo 源图、预览图、祝福语清单
```

## 更新记录

- **v2**：按宗教敏感性审查调整——祝福语改用天主教用语，删去「圣母垂怜」及有求必应、讲运气的句子（30 → 24 句）；
  祝福改为进入祭台时的「今日祝福」，不再作为「每满 10 朵」的奖励；缎带移到屏幕顶部，不遮挡圣母像；
  「+1」改为原地淡入淡出的小百合，计数写作「已献 N 朵」；修复花瓣雨在结束时突然消失（现随缎带一起淡出）。
- **v1**：首个版本。
