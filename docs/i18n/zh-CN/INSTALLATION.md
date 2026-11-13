# 安装

🇬🇧 [English](../../../INSTALLATION.md) · 🇫🇷 [Français](../fr/INSTALLATION.md) · 🇪🇸 [Español](../es/INSTALLATION.md) ·
🇨🇳 中文 · 🇮🇳 [हिन्दी](../hi/INSTALLATION.md) · 🇧🇷 [Português](../pt/INSTALLATION.md) · 🇷🇺 [Русский](../ru/INSTALLATION.md) ·
🇸🇦 [العربية](../ar/INSTALLATION.md) · 🇧🇩 [বাংলা](../bn/INSTALLATION.md) · 🇵🇰 [اردو](../ur/INSTALLATION.md)

请只使用[官方仓库发布页面](https://github.com/ibrahimbouillet/AetherShell/releases)上的安装程序。当前版本 3.5
[0.3.5-alpha.3](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.3.5-alpha.3) 是需要手动安装的预览版；
版本 1 的更全面验证仍在进行中。更早的 [0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2)
仍可获取。

请使用你平常的 Windows 用户帐户运行安装程序。自 Alpha 4 起，安装程序会使用你在 Aether Shell 设置中
选择的语言；如果没有选择，则使用你的 Windows 显示语言中第一个属于界面十种语言之一的语言，否则使用英语。安装向导会介绍产品、显示 MIT 许可证，并让你选择文件夹（默认
`%LOCALAPPDATA%\Programs\AetherShell`）、快捷方式和可选的开机启动。它会安装一个稳定的启动器、一个维护程序和带版本号的
程序文件。它可以创建开始菜单快捷方式，并在 Windows“已安装的应用”中添加一个按用户的条目。不使用管理员权限，也不替换
资源管理器。在全新的交互式安装中（自 Alpha 2 起），**登录时启动**会被明确勾选：如不需要，请在安装前取消勾选。接受
安装即保存该选择；已有的选择和任务管理器中的禁用设置都会被尊重。静默安装只有在使用显式参数 `--start-with-windows`
时才会启用开机启动；`--no-start-with-windows` 则将其关闭。便携配置文件在获得同意之前保持关闭。登录时的启动顺序由
Windows 控制，并可能被延迟。

V2 界面在 Aether Shell 启动时默认会在视觉上替换 Windows 任务栏。安装程序会说明这一行为；应用保留一个恢复助手，并可在
**设置 → 桌面**中关闭此功能。正常退出会恢复 Windows 任务栏。资源管理器继续运行，Windows 设置仍然只配置 Windows，
而不是 Aether Shell。恢复的限制和应急命令请见[使用指南](USER_GUIDE.md)。

未签名的应用或安装程序可能会触发 SmartScreen。此 Alpha 版没有 Authenticode 发布者证书。运行前请核对版本的确切来源及其
SHA-256：

```powershell
Get-FileHash .\AetherShell-0.3.5-alpha.3-win-x64-setup.exe -Algorithm SHA256
```

与对应的校验和文件进行比对。仅凭校验和并不能验证发布者身份。自动更新还需要
[UPDATE_AND_ROLLBACK](../../../UPDATE_AND_ROLLBACK.md)（英文）中所述的签名发布清单。

使用“已安装的应用 → Aether Shell → 修改”，可以用对应的安装程序修复当前版本。卸载会请求应用正常关闭，并且只删除该安装
所注册的条目和文件。默认保留用户数据；如需删除设置、缓存和日志，请明确选择删除。较旧的安装程序会被拒绝，除非明确允许
降级。在有意降级之前，请先备份你的数据。

下面的静默命令使用安装文件夹中的维护程序；下载的安装程序文件名包含版本号和架构。请使用绝对路径：

```powershell
.\AetherShellSetup.exe --silent --no-launch
.\AetherShellSetup.exe --silent --repair --no-launch
.\AetherShellSetup.exe --silent --uninstall --keep-user-data
```

选项：`--install-root <文件夹>`、`--data-dir <文件夹>`、`--no-shortcuts`、`--no-launch`、
`--start-with-windows`（明确同意）、`--repair`、`--uninstall`、`--keep-user-data`、
`--no-start-with-windows`（明确拒绝）、`--remove-user-data`、`--allow-downgrade`、`--log <文件>`。静默模式从不弹出
提示。退出代码：0 成功，1 操作失败，2 参数无效或路径准备失败。正常图形界面在操作进行期间不允许关闭；取消和自卸载的
验证情况记录在 [KNOWN_LIMITATIONS](../../../KNOWN_LIMITATIONS.md)（英文）中。
