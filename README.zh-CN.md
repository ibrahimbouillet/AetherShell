![Aether Shell——适用于 Windows 的原生模块化桌面界面](resources/branding/png/readme-banner.png)

<p align="center">
  🇬🇧 <a href="README.md">English</a> · 🇫🇷 <a href="README.fr.md">Français</a> · 🇪🇸 <a href="README.es.md">Español</a>
  · 🇨🇳 <strong>中文</strong> · 🇮🇳 <a href="README.hi.md">हिन्दी</a>
  · 🇧🇷 <a href="README.pt.md">Português</a> · 🇷🇺 <a href="README.ru.md">Русский</a> · 🇸🇦 <a href="README.ar.md">العربية</a>
  · 🇧🇩 <a href="README.bn.md">বাংলা</a> · 🇵🇰 <a href="README.ur.md">اردو</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/releases/latest"><strong>下载最新版本</strong></a>
  · <a href="docs/i18n/zh-CN/USER_GUIDE.md">使用指南</a>
  · <a href="KNOWN_LIMITATIONS.md">已知限制（英文）</a>
  · <a href="docs/knowledge/TEST_STATUS.md">测试记录（英文）</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml"><img alt="Windows CI" src="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="MIT 许可证" src="https://img.shields.io/badge/license-MIT-2ea44f.svg"></a>
  <img alt="Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg">
  <a href="https://www.youtube.com/@IbrahimBouillet"><img alt="YouTube 频道" src="https://img.shields.io/badge/YouTube-Subscribe-red?logo=youtube&logoColor=white"></a>
</p>

![Aether Shell 使用中：悬浮程序坞和顶部状态栏。Ibrahim Bouillet](./resources/branding/gif/AetherShell.gif)

# Aether Shell

由 **Ibrahim Bouillet** 打造的原生 Windows 桌面界面。Aether Shell 将悬浮程序坞与紧凑的顶部状态栏结合在一起，
采用柔和模糊的半透明材质。它使用 C++20、Win32、Direct2D/DirectWrite 和 Windows.UI.Composition 编写，以 MIT
许可证发布。

> **Alpha 软件：** 0.4.0-alpha.4（Alpha 4）是一个早期公开预览版。它还不是经过完整验证的 Windows
> 界面替代品。试用前请保存好工作，并阅读[已知限制](KNOWN_LIMITATIONS.md)。

<h2 align="center">全新的桌面，全新的氛围。</h2>

<p align="center">
  <img src="./resources/preview/aether-desktops.webp" alt="运行 Aether Shell 的五个桌面轮播：同样的程序坞和顶部栏，分别位于绿色山丘间的高架桥、碧绿的高山湖泊、砂岩峡谷、日落时的红色山脉和一张插画壁纸之上" width="100%">
</p>

<table>
  <tr>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-1.png"><img src="./resources/preview/thumbs/desktop-1.webp" alt="绿色山丘间高架桥上的 Aether Shell" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-5.webp"><img src="./resources/preview/thumbs/desktop-2.webp" alt="碧绿高山湖泊上的 Aether Shell" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-4.webp"><img src="./resources/preview/thumbs/desktop-3.webp" alt="砂岩峡谷上的 Aether Shell" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-3.webp"><img src="./resources/preview/thumbs/desktop-4.webp" alt="日落时红色山脉上的 Aether Shell" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-2.png"><img src="./resources/preview/thumbs/desktop-5.webp" alt="插画壁纸上的法语版 Aether Shell" width="100%"></a></td>
  </tr>
</table>

<p align="center"><sub>同一个程序坞和顶部栏，会融入你所选壁纸的色彩。点击图片可查看原尺寸。<br>
均为 Windows 11 上截取的真实桌面。壁纸和应用图标归其各自所有者所有，不受本项目许可证约束。</sub></p>

## 功能

- **程序坞：** 启动和切换应用、保留常用应用、重新排列固定的图标；用动画标记显示正在运行的应用，悬停时显示真实的窗口
  预览；打开搜索，并可进入“开始”和“任务视图”。
- **顶部状态栏：** 显示 Windows 提供的时钟、输入语言、网络、音频和电池状态。单击日期和时间可打开 Windows 通知和
  日历，一个小箭头可打开通知区域中的隐藏图标，状态组合按钮则会打开 Aether 的快捷控制面板及其 WLAN 列表。
- **搜索：** 查找并打开应用以及 Windows 自带的工具，例如设备管理器、服务或控制面板中的项目。
- **自动隐藏：** 程序坞和顶部栏可分别在应用窗口遮住它们时滑开，指针移到屏幕边缘时再回来，让你拥有全屏工作区。
- **41 种语言：** 整个界面（包括安装程序）支持 Windows 11 提供的 38 种语言，再加上孟加拉语、印地语和乌尔都语。它会跟随你的
  Windows 显示语言，也可以在“设置 → 语言”中自行选择，并在不同版本之间记住这一选择。每种语言都会在需要时从发布版本中下载。
  参见[语言](#语言)。
- **欢迎问候：** 可选功能。登录或解锁电脑后，在模糊的壁纸上用法语、英语或西班牙语手写一段问候。安装程序可以
  询问问候时使用的名字。
- **天气：** 可选择使用 Windows 位置权限获取大致位置和天气预报。如果无法定位或没有网络，天气会显示为不可用，
  而不会凭空猜测。
- **动效与外观：** 顶部栏、设置、菜单和安装程序采用同一种中性磨砂玻璃设计；设置页面滚动流畅；在 Aether 替换
  任务栏时，最大化窗口会显示圆角；支持深色/浅色外观、键盘导航和减少动效。
- **恢复：** 按用户安装、独立的恢复助手、修复和卸载支持，以及恢复 Windows 任务栏的命令。

## Alpha 4 新功能

两种风格：*液态*，覆盖在一层可由你调节的模糊层之上的清澈水玻璃；*磨砂*，即早期版本的磨砂材质。在液态风格下，
设置窗口只在自己的文字和控件下方模糊，其他地方保持清澈。界面支持 41 种语言，在你需要时从发布版本中下载，并跟随你的
Windows 显示语言。搜索能按名称找到“开始”菜单能找到的内容。天气精确且带有动画，还有自己的窗口。顶部栏可以只以纯文字
显示在桌面上，拖入图标时程序坞会随之伸展，登录后 Aether 也会更快启动。请阅读[版本说明](docs/releases/0.4.0-alpha.4.md)和
[更新日志](CHANGELOG.md)（英文）。

## 语言

Aether Shell 的界面支持 41 种语言：Windows 11 提供的 38 种，再加上孟加拉语、印地语和乌尔都语。每个菜单、面板、设置页面、
提示信息和安装界面都已翻译。Aether 会跟随你的 Windows 显示语言，或使用你在“设置 → 语言”中选择的语言；每种语言都会在
需要时从发布版本中下载。本文档提供下列十种语言的版本。

| | 语言 | 界面 | 文档 |
|---|---|---|---|
| 🇬🇧 | English | ✅ | [README](README.md) · [User guide](USER_GUIDE.md) · [Installation](INSTALLATION.md) |
| 🇨🇳 | 中文（简体） | ✅ | [README](README.zh-CN.md) · [使用指南](docs/i18n/zh-CN/USER_GUIDE.md) · [安装](docs/i18n/zh-CN/INSTALLATION.md) |
| 🇮🇳 | हिन्दी | ✅ | [README](README.hi.md) · [उपयोग गाइड](docs/i18n/hi/USER_GUIDE.md) · [इंस्टॉलेशन](docs/i18n/hi/INSTALLATION.md) |
| 🇪🇸 | Español | ✅ | [README](README.es.md) · [Guía de uso](docs/i18n/es/USER_GUIDE.md) · [Instalación](docs/i18n/es/INSTALLATION.md) |
| 🇫🇷 | Français | ✅ | [README](README.fr.md) · [Guide d'utilisation](docs/i18n/fr/USER_GUIDE.md) · [Installation](docs/i18n/fr/INSTALLATION.md) |
| 🇧🇷 | Português | ✅ | [README](README.pt.md) · [Guia do usuário](docs/i18n/pt/USER_GUIDE.md) · [Instalação](docs/i18n/pt/INSTALLATION.md) |
| 🇷🇺 | Русский | ✅ | [README](README.ru.md) · [Руководство](docs/i18n/ru/USER_GUIDE.md) · [Установка](docs/i18n/ru/INSTALLATION.md) |
| 🇸🇦 | العربية | ✅ （从右到左） | [README](README.ar.md) · [دليل الاستخدام](docs/i18n/ar/USER_GUIDE.md) · [التثبيت](docs/i18n/ar/INSTALLATION.md) |
| 🇧🇩 | বাংলা | ✅ | [README](README.bn.md) · [ব্যবহারকারী নির্দেশিকা](docs/i18n/bn/USER_GUIDE.md) · [ইনস্টলেশন](docs/i18n/bn/INSTALLATION.md) |
| 🇵🇰 | اردو | ✅ （从右到左） | [README](README.ur.md) · [صارف گائیڈ](docs/i18n/ur/USER_GUIDE.md) · [انسٹالیشن](docs/i18n/ur/INSTALLATION.md) |

**跟随 Windows**（默认设置）会按你排列的顺序查看你的 Windows 显示语言（设置 → 时间和语言），并使用其中第一个 Aether Shell
支持的语言（41 种中的任意一种）；如果都不是，则使用英语。你也可以在**设置 → 语言**中选择其他语言，更改会立即生效，并保存在每个版本都会读取的
设置中：安装新版本、修复或重新安装都会保留它，安装程序、更新程序和启动器也会使用这种语言。属于你或其他程序的名称（应用、
文件、WLAN 网络）永远不会被翻译。手写欢迎只能书写拉丁字母，因此会用法语、英语或西班牙语问候。阿拉伯语、希伯来语和乌尔都语从右向左
阅读：文字会相应地成形和对齐，而各个栏和面板保持原有布局。翻译文件位于 [tools/i18n/languages](tools/i18n/languages)，欢迎
提出修改建议。

## 替换 Windows 任务栏

V2 体验会在 Aether Shell 启动时隐藏 Windows 任务栏，并在原位置显示 Aether 程序坞。此行为默认开启，可在
**Aether Shell 设置 → 桌面**中关闭。资源管理器会继续运行；Aether Shell 不会替换 `explorer.exe`，不会修改
Winlogon 外壳，也不会在 Windows 设置中把自己注册为原生任务栏。Aether Shell 退出时，恢复助手会还原 Windows
任务栏。试用 Alpha 版之前，请先阅读[恢复指南](docs/i18n/zh-CN/USER_GUIDE.md)。

## 下载和安装

从 [GitHub 最新版本](https://github.com/ibrahimbouillet/AetherShell/releases/latest)下载 **x64 安装程序**，并将其
SHA-256 与发布页中的校验和文件进行比对。请使用你平常的 Windows 用户帐户运行安装程序。安装程序会说明任务栏的行为，
且不需要管理员权限。全新安装时会明确勾选**登录时启动**；你可以在接受之前取消勾选。已保存的选择以及任务管理器中的
禁用设置都会被尊重。安装程序还提供一个可选字段，用于填写欢迎问候时使用的名字；它只会保存在你的电脑上。已安装的
启动器可在有限次数的异常崩溃后重新启动应用；“退出”、恢复和卸载始终可用。启动时机由 Windows 决定，因此 Aether
无法保证登录时其界面一定先于原生任务栏出现。

此 Alpha 版需要手动安装。自动更新需要经过签名的发布清单；不会发布未签名的清单。安装程序没有 Authenticode 签名，
因此 Windows 可能会显示 SmartScreen 警告。参见[安装与验证](docs/i18n/zh-CN/INSTALLATION.md)。

## 兼容性

- Windows 11，x64：当前的开发目标。
- Windows 10 2004 及以上，x64：设计上支持，但此版本尚未进行运行时验证。
- ARM64：尚未进行运行时验证。

在 Windows 10 上，背景模糊使用较简单的色调。程序坞并未复现原生任务栏的所有功能，Windows 设置仍负责管理 Windows
任务栏。完整内容参见[已知限制](KNOWN_LIMITATIONS.md)。

## 构建与测试

在 Windows 上使用 Visual Studio C++ 生成工具、Windows SDK、CMake、Ninja 和 Python 进行构建：

```powershell
./scripts/build.ps1 -Config release -BuildDir build/release -Test -TestExclude 'network|integration' `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

构建按用户安装程序，并运行安装包的隐私与新鲜度审核：

```powershell
./scripts/package.ps1 -BuildDir build/release
```

模块级测试参见 [BUILDING.md](BUILDING.md)，测试隔离和手动验证参见 [TESTING.md](TESTING.md)，确切结果参见
[已执行测试记录](docs/knowledge/TEST_STATUS.md)（均为英文文档）。构建或测试套件通过，并不代表已在每个 Windows
版本或设备上完成验证。

## 项目文档

- [使用指南](docs/i18n/zh-CN/USER_GUIDE.md) · [安装](docs/i18n/zh-CN/INSTALLATION.md)
- 英文文档：[架构](ARCHITECTURE.md) · [项目规范](PROJECT_SPEC.md) · [任务状态](TASKS.md)
  · [更新与回滚](UPDATE_AND_ROLLBACK.md) · [安全策略](SECURITY.md) · [参与贡献](CONTRIBUTING.md)
  · [第三方声明](THIRD_PARTY_NOTICES.md)

贡献者和编程助手请从 [AGENTS.md](AGENTS.md) 和 [HANDOFF.md](HANDOFF.md) 开始。

## 许可证

MIT。参见 [LICENSE](LICENSE)。
