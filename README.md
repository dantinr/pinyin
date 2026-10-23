# PenBlade · 笔锋输入法

**默认离线，个人词库由你掌控。**

PenBlade 是一款面向 Windows 的隐私优先中文拼音输入法，支持全拼、简拼混输、整句输入、本地学习和个人词库管理。日常输入在本地完成，也可以主动连接自己的词库同步服务，在设备之间复用个人词库。

[下载 Windows x64 发布包](https://github.com/dantinr/pinyin/releases/latest) · [快速开始](#快速开始) · [使用文档](#使用文档) · [问题反馈](https://github.com/dantinr/pinyin/issues)

**当前已有可安装使用的开发预览版，面向 Windows x64（AMD64）桌面应用，程序尚未签名。** 应用兼容性仍在验证中；下载页若未显示预览版本，请查看[全部 Releases](https://github.com/dantinr/pinyin/releases)。源码版本以 [VERSION](VERSION) 为准，发布包版本以对应 Release 为准。

PenBlade · 笔锋输入法是项目展示名称。安装后在 Windows 中仍显示为 **“隐私拼音（开发版）”**，程序文件、内部标识和数据目录继续沿用 `PrivatePinyin` / `private_pinyin` 命名。

## 为什么做这个输入法

输入习惯和个人词库应当由使用者掌控。日常中文输入可以在本地完成，记住的表达也应当能看见、备份、导出，并继续维护。

因此，PenBlade 默认在本地转换拼音、学习词语，把个人词库保存在可读的 TSV 文件中。跨设备同步由用户主动开启，服务地址由用户指定；不需要同步时，无需配置服务器或账号。

## 核心能力

| 用途 | 当前支持 |
| --- | --- |
| 少打一些拼音 | 全拼、简拼和混输；补全最后一个尚未打完的音节，例如 `niha` 可选“你好”。 |
| 连续输入一句话 | 按本地词库离线组词；整句不合适时可先确认一段，再继续改选剩余拼音。 |
| 复用自己的表达 | 默认开启本地学习，记住确认的词语、完整读音与选择次数；分段组成的新词和已确认长句也能复用。 |
| 切换中英文与标点 | 单独按 Shift 或点击“中 / 英”切换；支持中文标点、保留大小写的英文组合输入，以及试用中的自动英文识别。 |
| 管理自己的词库 | 图形界面导出个人词库、合并到用户主词库；CLI 导入、合并其他词库，按主题启停辅词库。可选连接自己的云同步服务。 |
| 查看与维护使用数据 | 本地输入量、估算速度、每日统计及最近 30 天新词增长曲线；脚本或用户自己的 Agent 可通过返回 JSON 的 CLI 管理词库。 |

下面展示候选能力，实际顺序会受词库、读音歧义和个人使用记录影响，不保证所有输入都首选正确：

| 输入 | 可以得到的候选 | 方式 |
| --- | --- | --- |
| `nihao` | 你好 | 全拼 |
| `nh` / `nhao` | 你好 | 简拼 / 混输 |
| `niha` | 你好 | 末尾音节补全 |
| `wozaibeijingshangban` | 我在北京上班 | 离线整句组词 |

常用按键与输入规则见[输入指南](docs/input-guide.md)，词库范围与数量集中维护在[基础词库说明](data/README.md)。

## 快速开始

1. 在 [Releases](https://github.com/dantinr/pinyin/releases/latest) 下载 Windows x64 ZIP 安装包；不要下载 GitHub 自动生成的 **Source code** 压缩包。预览版本也可从[全部 Releases](https://github.com/dantinr/pinyin/releases) 查找。
2. **完整解压** ZIP，不要直接在压缩包中运行程序。
3. 双击 `install.cmd`，同意管理员权限提示。安装器会把文件复制到 `%ProgramFiles%\PrivatePinyin\版本号`，必要时安装包内附带的 Microsoft VC++ x64 运行库，然后注册输入法；安装过程无需联网。
4. 按 **Win + Space**，选择 **“隐私拼音（开发版）”**。安装不会修改系统默认输入法。
5. 重新打开需要输入的应用。在普通文本框输入 `nihao`，看到“你好”候选后按空格确认，验证中文输入。

安装后可删除下载的 ZIP 和解压目录，**安装目录必须保留**。右键语言栏 / 任务栏的“中 / 英”按钮可以打开设置；找不到按钮时，直接运行安装目录中的 `private_pinyin_settings.exe`，无需管理员权限。

**更新与卸载：** 新版仍运行 `install.cmd`，每个版本安装到独立目录；重新打开应用后才会加载新版。卸载时运行当前安装版本目录内的 `uninstall.cmd`，确认管理员权限；卸载保留个人词库及共用的 VC++ 运行库，并关闭本输入法的同步设置和登录启动项。关闭加载旧 DLL 的应用后，可手动删除对应版本目录。完整说明见[发布与安装文档](docs/releases.md#下载后的安装)及[安装包使用说明](packaging/README.txt)。

## 隐私与数据

**不记录逐键日志或完整输入流水，不等于不保存输入内容：本地学习会保存你确认的词语，也可能包括完整长句。** 普通隐私文本框仍可输入并学习；密码 / PIN 框直接输入，不做中文转换或学习。

| 项目 | 当前行为 |
| --- | --- |
| 默认是否联网 | 默认离线，无遥测和剪贴板访问。输入法 DLL 本身不发起网络请求；可选同步由独立程序 `private_pinyin_sync.exe` 执行。 |
| 本地学习 | 系统输入法默认开启；成功确认后保存词语或长句、完整读音和选择次数，也保存明确分段确认的多音节词。不保存取消的组合、直接提交的拼音原文或英文原文输入。 |
| 本地统计 | 保存每日中文确认字数、上屏次数、有效输入时长及每日新增词汇数量；不含原文、拼音、应用名称或逐次输入时间戳。速度由汇总数据估算。 |
| 数据位置 | 默认 `%USERPROFILE%\PrivatePinyin`；个人学习词库为 `words.user.tsv`，用户主词库为 `main.tsv`，主题辅词库在 `dictionaries` 目录。完整文件表及旧目录迁移见[用户数据说明](docs/user-data.md)。 |
| 文件是否加密 | 词库、统计和备份为本地明文，其他有权限的本机程序可以读取。同步令牌例外，使用 Windows 当前用户的 DPAPI 加密保存；同步地址不加密。 |
| 如何开启同步 | 设置 →“云同步…”→ 填写自己的服务地址及可选令牌 → 勾选启用并保存。首次立即同步，此后约每 10 分钟同步，登录后恢复；服务需支持 GET、条件 PUT 和强 ETag。 |
| 同步发送什么、到哪里 | 向你配置的地址发送个人词语、完整读音、选择次数及同步变更日期；首次可能上传整个个人词库，PUT 发送完整合并档案。不上传基础词库、`main.tsv`、辅词库、输入统计或本地设置。令牌仅用于请求认证。 |
| 关闭学习 | 不再使用或新增个人学习词，保留已存文件；也丢弃当前应用尚未保存的学习。已合并到 `main.tsv` 的词和主题辅词库仍可使用，数字统计继续累计。 |
| 清空个人词库 | 清空 `words.user.tsv` 的词条及选择次数，保留学习开关；不删除 `main.tsv`、辅词库、统计、备份、旧迁移文件或云端记录。 |
| 关闭同步 | 停止后台同步并移除登录启动项，保留本地与服务端已有数据；不改变本地学习开关。 |

学习与同步是**两个独立开关**：关闭学习不会停止上传已有词库，关闭同步也不会停止本地学习。**同步采用增量合并，不传播删除：清空本地词库后，云端词条可能在下次同步恢复。** 如需清理所有副本，先在所有设备关闭同步，再分别处理本地文件、备份与服务端档案，详见[同步合并与删除规则](docs/cloud-sync.md#合并与日期)。

学习开关可通过“中 / 英”右键菜单调整，也可在安装目录打开 PowerShell，运行：

```powershell
.\private_pinyin.exe --ime-learning status
.\private_pinyin.exe --ime-learning off
# 以下命令会清空个人学习词库；请先按上文处理同步并备份。
.\private_pinyin.exe --ime-learning clear
```

重新开启使用 `--ime-learning on`。导出文件同样可能包含个人表达，请按个人数据保管。

## 使用文档

| 文档 | 内容 |
| --- | --- |
| [输入指南](docs/input-guide.md) | 选词、分段改选、简拼、补全及中英文输入 |
| [状态与设置](docs/settings.md) | 打开设置、语言栏按钮、学习与标点开关 |
| [个人词库](docs/personal-dictionaries.md) | 导出、导入、备份、合并到用户主词库 |
| [Agent 词库接口](docs/agent-dictionaries.md) | 主题辅词库、JSON CLI、校验与维护规则 |
| [个人词库云同步](docs/cloud-sync.md) | 服务要求、自建示例、传输内容及清理规则 |
| [输入统计](docs/input-statistics.md) | 输入量、速度口径、每日新增及增长曲线 |
| [数据目录与迁移](docs/user-data.md) | 文件位置、旧版迁移、备份及故障检查 |
| [基础词库](data/README.md) | 当前规模、来源、格式和质量边界 |
| [发布与安装](docs/releases.md) | 打包、版本规则、安装、更新和卸载 |

## 当前限制

- **平台：** 当前发布包及输入法 DLL 面向 Windows x64（AMD64）桌面应用；暂不支持 32 位应用、原生 ARM64、AppContainer、安全桌面或触摸键盘。
- **输入功能：** 尚不支持双拼、模糊音和拼写纠错；末尾补全只补最后一个已输入音节，不预测后续音节或修复中间漏字。
- **候选质量：** 整句组词基于词库与启发式权重，不是训练过的语言模型。同音词、多音字和长句可能需要改选；基础词库尚未经过独立语言专家审校或真实语料词频评估。
- **试用功能：** 自动英文识别默认开启，依赖常用词表，未知单词、人名和中英歧义可能误判，可用 Shift 手动切换。
- **兼容性与签名：** 已有 TSF、候选窗口、运行库兼容性及安装包自动测试，不代表所有应用均已验证。项目程序尚未代码签名；正式签名、更多真实应用验证与候选质量评测仍是后续工作。

## 开发与贡献

使用 **C++17、Windows SDK / TSF、CMake**，不依赖第三方输入法引擎。先安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载及 CMake，在仓库根目录运行：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

PowerShell 7 和 Python 3 可启用相应 CLI、安装包及 HTTP 同步集成测试。测试覆盖词库、输入状态、学习、TSF 上屏与焦点、候选窗口、统计、同步和打包；个人数据测试使用独立临时目录。无需系统注册即可运行 `build\Release\private_pinyin_demo.exe` 体验实际 TSF 组合输入，前提是已安装匹配的 VC++ 运行库。

| 模块 | 入口 |
| --- | --- |
| 词库、候选与组合状态 | `src/lexicon.cpp`、`src/sentence_decoder.cpp`、`src/input_session.cpp` |
| Windows TSF 与候选窗口 | `src/ime/` |
| 本地学习与词库管理 | `src/learning_dictionary.cpp`、`src/user_store.cpp`、`src/dictionary_manager.cpp` |
| 设置、统计与独立同步 | `src/settings_main.cpp`、`src/input_statistics.cpp`、`src/vocabulary_growth.cpp`、`src/sync_*.cpp`、`src/dictionary_sync.cpp` |
| CLI 与测试 | `src/main.cpp`、`src/dictionary_cli.cpp`、`tests/` |

开发注册 / 卸载、CLI 调试、算法边界、TSF 回调和锁机制见[开发文档](docs/development.md)。欢迎通过 [Issues](https://github.com/dantinr/pinyin/issues) 提交问题，附 Windows 与应用版本、应用架构、输入法版本、拼音及复现步骤；涉及个人词库时请提供脱敏的最小样例。

仓库目前尚未提供 `LICENSE` 文件，代码和词库的使用、修改及分发许可仍需维护者明确。
