# 中英文状态与本地设置

当前版本为 0.6.0。切换到隐私拼音后，Windows 语言栏或任务栏输入模式位置显示“中 / 英”。点击按钮或单独按 Shift 切换，两种操作同步更新图标、文字和键盘行为。中英文模式属于当前应用会话，新激活的输入法默认中文；学习和标点开关是所有应用共享的持久化设置。

右键按钮（或点击语言栏下拉箭头）可切换“本地学习”“中文标点”，打开“设置…”或个人词库目录。也可直接运行安装目录中的 `private_pinyin_settings.exe`；它无需管理员权限，不依赖目标应用窗口。

## 开关行为

- 本地学习默认开启。关闭后不使用或新增个人词，不删除已有词库；重新开启可继续使用。它和原有 `private_pinyin.exe --ime-learning on|off` 命令共用同一开关。
- 中文标点默认开启。关闭后输出英文符号；输入拼音时仍可用标点确认当前候选，例如 `nh, → 你好,`。空闲英文符号直接交给应用；拼音单引号分隔、网址、数字和密码/PIN 行为保持原有规则。
- 修改勾选框自动保存，无需额外点击应用。已运行的输入法在下一次按键或切回应用时刷新设置，关闭学习也会移除尚未确认候选中的个人词；已经选定的分段文本会保留。

设置与个人词库位于 `%LOCALAPPDATA%\PrivatePinyin`。学习关闭标记为 `words.user.tsv.ime-learning.disabled`，标点关闭标记为 `words.user.tsv.punctuation.disabled`；没有标记即默认开启。每个开关独立保存，修改标点不会覆盖另一应用的学习设置。读取设置不创建词库，不保存应用文本或按键。

## 已安装开发版本的更新

0.5.0 新增了任务栏支持的 TSF 类别。已有 0.4.0 或更早版本的开发注册，需要在更新 DLL 后重新运行注册脚本一次；发行包的 `install.cmd` 会自动完成。不要仅替换 DLL 后就假定任务栏已经启用新入口。

在**管理员身份的 64 位 PowerShell** 中运行以下命令，可同时避开当前终端的脚本执行策略限制：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\codex_projects\private-pinyin\scripts\register-ime.ps1" -DllPath "D:\codex_projects\private-pinyin\build\Release\private_pinyin_ime.dll"
```

重新打开需要输入的应用并切换到隐私拼音。DLL 同目录需保留 `data\base.tsv` 和 `private_pinyin_settings.exe`。图标的显示位置受 Windows 语言栏配置影响；即使图标被系统隐藏，也可以直接打开设置程序。

实现使用微软的 [语言栏接口](https://learn.microsoft.com/en-us/windows/win32/tsf/language-bar) 和 [任务栏支持类别](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-category-values)，没有独立常驻托盘进程。
