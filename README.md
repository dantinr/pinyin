# Private Pinyin：Windows 中文拼音输入法开发版

使用 C++17、Windows SDK 和 TSF 编写，包含词库核心、命令行原型、x64 输入法 DLL 和独立开发测试窗口。当前是使用小型演示词库的开发版，未签名；系统注册后仍需逐个验证目标应用兼容性。独立测试窗口无需管理员权限或系统注册即可体验实际 TSF 组合文本。

## 已实现

- 按音节建立 Trie，查询连续全拼，例如 `nihao`、`chongqing`。
- 保留歧义切分：`xian` 可以匹配“先”和“西安”；`xi'an` 仅匹配后者。
- 处理大小写、空格和 `ü → v`；输入和词库不使用声调。
- 词级多音字读音；同词不同读音保留，同词同读音取较大权重。
- 基于词权重的候选排序；可选的本地选词次数加分，设有上限。
- 可选自造词、用户词库加载和清空；同目录临时文件加原子替换，使用 Windows 独占文件锁避免多进程覆盖更新。
- 严格校验 TSV、音节、UTF-8、权重，报错包含文件行号；基础词库加载失败不影响原有数据。
- 基础词库路径默认相对于可执行文件，支持中文目录和中文命令行参数。
- TSF COM DLL、启用/停用、焦点与文档事件、同步和异步编辑会话。
- 组合文本、下划线显示属性、光标左右移动、Home/End、插入、退格和删除。
- 不抢焦点的候选窗口、数字/鼠标选词、上下选择、分页、DPI 缩放和屏幕边缘定位。
- TSF 候选 UI 元素，让请求 UI-less 模式的应用可以读取候选。
- 单独按 Shift 切换中英文；空格选中文，Enter 提交拼音原文，Esc 取消。
- 应用禁用输入法、只读输入框、敏感 InputScope 时跳过输入；失焦保留拼音原文并结束组合。
- 开发注册/卸载脚本，只操作本输入法的 COM、TSF 项。

## 构建与测试

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载和 CMake。在开发者 PowerShell 中运行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

核心测试覆盖拼音歧义、多音字、排序、损坏词库和原子存储；组合状态测试覆盖编辑和分页。TSF 测试通过进程内 COM 工厂和 `TF_RP_LOCALPROCESS` 注册，使用 Windows 提供的真实文档、范围和组合对象，验证上屏、取消、退格、焦点、只读/禁用状态、密码/隐私/PIN InputScope 和资源释放。测试不永久注册输入法，不改系统默认输入法。

配置时检测到 PowerShell 7（`pwsh`）还会加入 CLI 集成测试，检查默认无痕不创建用户目录、显式学习、自造词、重启加载和清空；测试使用独立临时目录，不访问真实用户词库。

对于 Visual Studio 多配置生成器，可执行文件位于 `build\Release\private_pinyin.exe`，构建时自动复制示例词库。其他生成器的可执行文件位置可能不同。

```powershell
.\build\Release\private_pinyin.exe --query nihao
.\build\Release\private_pinyin.exe --query "xi'an"
.\build\Release\private_pinyin.exe
.\build\Release\private_pinyin.exe --learn
```

## 体验 TSF 输入法

```powershell
.\build\Release\private_pinyin_demo.exe
```

点击测试窗口白色文档区域，输入 `nihao`，候选应为“你好”“拟好”，按空格选“你好”、按数字 `2` 选“拟好”。也可以输入 `chongqing` 或 `xi'an`。未输入完整词条时仅显示拼音，无补全候选。

独立窗口采用专用的 TSF 测试文档，只激活本进程的输入法，便于验证 DLL，不代表记事本、浏览器或 Office 已经通过兼容性测试。测试窗口没有文件保存功能。

## 系统注册与卸载（开发用）

在**管理员身份的 64 位 PowerShell** 中执行，指定已编译的 DLL：

```powershell
.\scripts\register-ime.ps1 -DllPath .\build\Release\private_pinyin_ime.dll
```

它注册简体中文 `0x0804` 的“隐私拼音（开发版）”。通过 Windows 输入法选择器切换到它，再在新打开的记事本或浏览器文本框中验证 `nihao → 空格 → 你好`。注册不修改默认输入法，不替换其他输入法。注册后 DLL 与同目录 `data\base.tsv` 必须保留在原位置。

```powershell
.\scripts\unregister-ime.ps1 -DllPath .\build\Release\private_pinyin_ime.dll
```

卸载脚本只移除本项目注册项，不删除词库或其他输入法。已加载 DLL 的应用退出后才会释放文件，因此更新前应切换输入法并关闭使用旧 DLL 的应用。脚本不自动请求提权、不关闭用户应用。

当前 DLL 面向 x64 桌面应用。暂不支持 32 位应用、AppContainer/安全桌面、触摸键盘和完整语言栏模式图标。正式发布还需要代码签名、稳定安装目录、应用兼容性测试和升级回滚。

## 使用与隐私

TSF DLL 始终无痕，只读取基础词库；不读取或写入用户词库，不学习。命令行程序默认同样不读取、创建或写入用户词库。程序没有网络调用、输入日志、遥测或剪贴板访问，没有第三方业务库依赖。运行依赖 Microsoft C++ 运行时和 Windows 系统组件，发布时需要部署匹配的 C++ 运行时。

命令行程序显式传入 `--learn` 后，在 `%LOCALAPPDATA%\PrivatePinyin\words.user.tsv` 保存“词语、拼音、选择次数”。可用 `--user 路径` 指定位置。数据为本地明文，不提供静态加密，也不能防止其他有权限访问文件的本机程序读取它。锁文件仅用于并发控制，不含输入记录。

交互时输入拼音展示最多 10 个候选，输入数字确认；查询本身不学习，只有确认选词才增加计数。`--query` 始终使用基础词库，不读取或写入个人数据，即使同时传入 `--learn`。

```text
> nihao
1. 你好  [ni hao]
2. 拟好  [ni hao]
> 1
确认：你好
> /add yin'si'shu'ru'fa 隐私输入法
自造词已保存。
> yinsishurufa
1. 隐私输入法  [yin si shu ru fa]  本地选择 1 次
> /clear
个人词条和选择次数已清空。
> /quit
```

`/add` 和 `/clear` 需要以 `--learn` 启动。默认模式拒绝保存自造词。`/clear` 清空此用户词库的当前逻辑内容，不清理外部备份。

## 词库格式

`data/base.tsv` 是手工编写的演示数据，权重为示例数值，未导入第三方词库，不能用于评估真实输入准确率。每行由实际 TAB 分隔：

```text
你好<TAB>ni hao<TAB>10000
拟好<TAB>ni hao<TAB>100
重庆<TAB>chong qing<TAB>7500
重量<TAB>zhong liang<TAB>6500
```

- UTF-8，支持 BOM、CRLF、空行和以 `#` 开头的注释。
- 必须恰好三列；权重是 1 至 1,000,000,000 的整数。
- 词条读音必须按音节分隔，`nihao` 不能直接作为两音节词条读音。
- 查询输入可连续输入，显式分隔符用于约束音节边界。
- 用 `--dict 文件` 替换基础词库。第三方数据需要在构建阶段转换为该格式并保留来源及许可信息。

## 模块与后续工作

`include/pinyin/lexicon.hpp` 是词库接口，`src/lexicon.cpp` 实现解析、索引和排序；`src/input_session.cpp` 是与平台无关的组合状态机；`src/user_store.cpp` 负责命令行的本地存储；`src/main.cpp` 是命令行入口。

`src/ime/text_service.cpp` 接入 TSF，`candidate_window.cpp` 提供候选窗口及 UI 元素，`display_attribute.cpp` 提供组合下划线，`module.cpp` 提供 DLL 导出、COM 工厂和系统注册。`src/ime_demo.cpp` 与 `tests/tsf_text_store.hpp` 是开发测试宿主，实际 TSF DLL 不依赖测试宿主。

当前匹配完整词条，不支持未完成音节补全、简拼、模糊音、纠错、双拼或自动整句组合。用户只输入 `niha` 不会得到“你好”；“我在北京”必须是一个词条才会整体匹配。排名算法是原型，并非训练过的语言模型；用户加分有上限，暂未实现时间衰减。单字多音字表不能自动推导正确的多音词读音，需要词条显式标注。

建议后续依次完成：应用兼容性验证、正式词库和质量评测、拼音切分图与整句解码、语言栏与标点策略、签名安装和升级。

TSF 接口设计参考微软 [Text Service Registration](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)、[Compositions](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions)、[RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)。未引入小狼毫、Rime 或微软 SampleIME 源码。
