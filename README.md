# 圣母献花 mary_c3

ESP32-C3 SuperMini + 1.50 寸 240×280 SPI 彩屏（GC9306 / GC9307）做的献花小程序，基于 **ESP-IDF v6.1**。
按一下板载 BOOT 键，就向圣母献上一朵花；献花数和电子木鱼一样保存在 Flash 里。

![preview](art/preview.png)

预览图从左到右：开机画面 / 献花中（花飞入花瓶、+1）/ 每满 10 朵的祝福画面。

## 玩法

| 画面 | 操作 | 效果 |
|---|---|---|
| 开机画面 | 单击 BOOT | 进入祭台 |
| 祭台 | 按下 BOOT | 献花一朵，按下即响应 |
| 祭台 | 长按 BOOT **1 秒** | 献花数清零，并从 Flash 删除，提示「献花数已清零」 |

- **开机**：logo 图铺满全屏，白色花瓣缓缓飘落，底部浅蓝缎带风格的「点击献花」按钮呼吸闪烁。
- **祭台**：
  - 场景：金色拱形神龛里是圣母像（从 logo 图裁出），背后是蓝色彩窗和柔光，两侧烛光闪烁，祭台铺白色金边桌布，中间一只白瓷花瓶。
  - **献花**：每按一下，一朵百合或白玫瑰从下方沿弧线飞进花瓶，拖着星光。落下时星光四散、神龛泛起金光，右上角升起「+1」，顶部居中的献花数跳一下后加 1。
  - 前 20 朵依次插满花瓶，满了以后新献的花会飞入花束中随机一处。
  - 连按时每一下都会计数，最多同时 6 朵在空中。
- **每满 10 朵**：白色花瓣如雨飘落，神龛持续发光，落下一条浅蓝缎带，上面随机显示一句祝福（共 30 句，
  见 [`art/blessings.txt`](art/blessings.txt)），约 3.6 秒后淡出。
- 献花数停止操作 1 秒后写入 Flash（NVS），断电重启后接着计数。

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
cd <本工程目录>
idf.py set-target esp32c3        # 首次
idf.py build
idf.py -p COMx flash monitor
```

生成好的图片和字形都已入库，直接编译即可。

- 屏幕上的「点击献花」指按板载 **BOOT 键**（本项目没有接触摸）。
- 烧录不进时：按住 BOOT，点一下 RESET，松开 BOOT，再烧录。

## 资源占用

| | 大小 |
|---|---|
| 固件 | 约 0.73 MB（程序分区 3.9 MB，剩 82%） |
| 开机图 + 祭台背景 | 2 × 134 KB |
| 精灵图（百合 ×3、白玫瑰 ×2、满天星、花瓣 ×4、烛火、光晕、缎带、按钮等） | 约 160 KB |
| 祝福语楷体字形（88 字，带描边） | 约 57 KB |
| RAM | 静态约 60 KB + 送屏缓冲 2 × 19 KB |

## 美术与文字

- [`tools/make_art.py`](tools/make_art.py)：开机图缩放（源图 `art/logo_src.png`）；祭台背景分三层合成——
  教堂（彩窗、光束、烛光虚化），从源图裁出的圣母像贴进拱形神龛，前景（神龛金框、祭台、花瓶、蜡烛、计数面板）；
  花、花瓣、烛火、缎带等用 SVG 画，无头 Edge/Chrome 渲染成带 alpha 的精灵图。花束的 20 个位置也在这里定义。
- [`tools/make_blessings.py`](tools/make_blessings.py)：祝福语，生成 `main/blessings.h`（楷体带描边字形），
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
  main.c        开机 / 祭台两个画面、献花飞行与计数、每 10 朵的花瓣雨与祝福、NVS 保存 / 清零
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
