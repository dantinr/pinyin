# 开发、测试与实现说明

本文的命令均在仓库根目录运行。项目展示名称为 PenBlade · 笔锋输入法；源码、构建产物、系统注册名称和数据目录沿用 Private Pinyin。普通用户安装请先看 [README 快速开始](../README.md#快速开始)。

## 构建与测试

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载和 CMake。在开发者 PowerShell 中运行：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

核心测试覆盖拼音歧义、多音字、排序、损坏词库和原子存储；组合状态测试覆盖编辑和分页。TSF 测试通过进程内 COM 工厂和 `TF_RP_LOCALPROCESS` 注册，使用 Windows 提供的真实文档、范围和组合对象，验证上屏、取消、退格、焦点、只读/禁用状态、密码/隐私/PIN InputScope 和资源释放。测试不永久注册输入法，不改系统默认输入法。

候选窗口测试验证首次绘制、提交后销毁、宿主窗口关闭后重建、鼠标回调和反复输入不累积隐藏弹窗。TSF 集成测试还模拟应用延迟授予读写锁，检查失焦、停用后执行旧布局回调时不会再次显示候选框。

TSF 回调测试覆盖应用在候选窗创建、更新、关闭通知中同步失焦或停用输入法，检查无崩溃、无残留 UI 注册且后续输入恢复正常。候选窗口类的共享锁使用 Windows 原生 SRW 锁，避免宿主预先加载旧版 `MSVCP140.dll` 时，新工具链的 `std::mutex` 初始化方式与旧运行库不兼容。

MSVC 构建包含独立的运行库兼容性测试宿主。它静态链接自身运行库，再预加载目标运行库并激活实际输入法 DLL，检查反复输入、选词、取消及资源释放。默认 CTest 使用系统运行库；可另行指定含 `MSVCP140.dll`、`VCRUNTIME140.dll` 和 `VCRUNTIME140_1.dll` 的旧运行库目录：

```powershell
.\build\Release\host_runtime_tsf_tests.exe .\build\Release\private_pinyin_ime.dll "C:\path\to\old-runtime"
```

词库测试检查实际基础词库的格式、重复项、字数与音节数，并抽查常用词、多音词和同音候选排序。核心、组合状态和 CLI 测试使用固定的小词库样本，避免词库扩充改变测试预期。TSF 测试用独立的进程内注册标识实例化同一 DLL，避免与系统已安装的输入法冲突，并检查扩充词库中的单词和短语能实际上屏。学习和 TSF 集成测试使用独立临时用户目录，不读取真实个人数据；模拟按键用 Windows 的线程键盘状态表隔离桌面修饰键状态。

配置时检测到 PowerShell 7（`pwsh`）还会加入 CLI 和设置窗口等集成测试，检查命令行交互默认不学习、显式学习、自造词、重启加载和清空，以及系统输入法默认开启学习、显式关闭和重启后保留设置；测试使用独立临时目录，不访问真实用户词库。检测到 Python 3 时会加入 HTTP 同步集成测试；测试清单以当前 CMake 配置为准，可用 `ctest --test-dir build -C Release -N` 查看。

对于 Visual Studio 多配置生成器，可执行文件位于 `build\Release\private_pinyin.exe`，构建时自动复制基础词库。其他生成器的可执行文件位置可能不同。


## 系统注册与卸载（开发用）

在**管理员身份的 64 位 PowerShell** 中执行，指定已编译的 DLL：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\register-ime.ps1 -DllPath .\build\Release\private_pinyin_ime.dll
```

它注册简体中文 `0x0804` 的“隐私拼音（开发版）”。通过 Windows 输入法选择器切换到它，再在新打开的记事本或浏览器文本框中验证 `nihao → 空格 → 你好`。注册不修改默认输入法，不替换其他输入法。注册后 DLL 与同目录 `data\base.tsv` 必须保留在原位置。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\unregister-ime.ps1 -DllPath .\build\Release\private_pinyin_ime.dll
```

上述开发卸载脚本只移除本项目注册项，不删除词库或其他输入法，也不管理独立同步程序；启用过同步时请先在设置中关闭。发布包的 `uninstall.cmd` 会同时关闭同步，见[发布流程](releases.md#下载后的安装)。已加载 DLL 的应用退出后才会释放文件，因此更新前应切换输入法并关闭使用旧 DLL 的应用。开发脚本不自动请求提权、不关闭用户应用。命令中的执行策略参数仅作用于本次 PowerShell 进程。

当前 DLL 面向 Windows x64（AMD64）桌面应用。暂不支持 32 位应用、原生 ARM64、AppContainer、安全桌面和触摸键盘。发布包已使用独立版本安装目录，注册失败时尝试恢复旧 DLL 注册；代码签名及更多真实应用兼容性验证仍待完成。

## 学习与并发存储

TSF 输入成功确认后，通过 `LearningDictionary::remember` 保存完整词语或长句、完整读音和选择次数，以及明确分段选择的多音节词。原样 Enter 提交没有确认读音，不进入学习。学习开关在下一轮输入刷新，关闭后不使用个人学习词库，并丢弃待保存记录；用户主词库与主题辅词库仍独立加载。

`src/user_store.cpp` 使用 Windows 独占文件锁保护短暂的读、合并与写入，并通过同目录临时文件原子替换发布完整快照，避免多应用覆盖彼此的词条。新一轮输入读取已发布快照，不获取写入锁；读取允许文件替换，写入遇到短暂拒绝时保留临时文件并重试。

读写失败不阻止上屏。正在运行的应用保留最后一次成功读取的词库，保存失败的学习暂存在内存，后续输入重试；应用退出前若一直未保存成功，这部分记录会丢失。损坏文件不会被空数据覆盖。“学习暂不可用”表示尚有读写失败。CLI 的 `--learn` 交互模式在整个会话持有写锁，调试时建议用独立 `--user` 文件。

默认共享目录及 MSIX 旧数据迁移见[用户数据](user-data.md)。词库、统计和备份是明文；同步令牌由 DPAPI 加密。网络依赖位于独立的 `pinyin_sync` 模块与同步程序，输入法 DLL 不链接该模块，详见 [CMake 配置](../CMakeLists.txt)及[云同步说明](cloud-sync.md)。

## 模块与算法边界

`include/pinyin/lexicon.hpp` 是词库接口，`src/lexicon.cpp` 实现解析、索引和排序，`src/sentence_decoder.cpp` 实现有界整句解码；`src/input_session.cpp` 是与平台无关的组合状态机；`src/user_store.cpp` 提供学习、导入及同步等路径共用的个人词库存储；`src/main.cpp` 是命令行入口。

`src/ime/text_service.cpp` 接入 TSF，`candidate_window.cpp` 提供候选窗口及 UI 元素，`display_attribute.cpp` 提供组合下划线，`module.cpp` 提供 DLL 导出、COM 工厂和系统注册。`src/ime_demo.cpp` 与 `tests/tsf_text_store.hpp` 是开发测试宿主，实际 TSF DLL 不依赖测试宿主。

输入法支持整词匹配、简拼及混输、末尾音节补全、自动整句组合和前缀分段选词，不支持模糊音、纠错或双拼。用户输入 `niha` 可直接选“你好”，也可先选“你”后独立改选最后一个字。补全只扩展最后已输入音节，不预测后续音节，不修补中间漏字；未确认时仍显示实际输入的拼音，Home/退格恢复实际拼写。相同简拼数量的词条中，完整匹配优先于补全；整句补全有排序惩罚，仍需按候选确认正确词语，例如 `shangba` 可完整匹配“伤疤”，也可补全为“上班”。学习始终保存被确认词语的完整读音，全拼、简拼与补全共用个人记录。

整句解码在每个拼音位置保留最多 16 条路径，同一结束位置保留最多 16 个词候选，生成最多 5 个整句候选，以控制搜索量和保持输入响应；没有固定的音节、拼音或输出长度上限。已处理的位置及时释放搜索路径，词库匹配使用迭代遍历，长词条不会增加原生调用栈深度。拼写精度相同时，完整词条优先于组合候选，词级读音和显式分隔符参与切分；不会通过两个单字为已知双字词另造读音。

简拼按每个音节的首字母匹配，零声母音节也使用首字母，例如 `x'a → 西安`。`zg` 和 `zhg` 都可匹配“中国”，`cq` 和 `chq` 都可匹配“重庆”，全拼和简拼可在任意音节混用。相同匹配范围内，完整拼音优先，其次是缩写音节较少的候选，再按本地选择次数和权重排序。分段选字仅消耗实际输入的简拼字母，Home 可恢复原始拼写；学习只保存完整读音，因此全拼、简拼和混输复用同一个用户词条及计数。自造词的 `/add` 读音仍填写完整音节。

整句排名使用人工词权重、分词数量惩罚和本地选择次数，并非训练过的语言模型，不能保证任意句子的语法、同音词或多音字都正确。已选长句可学习成完整词条；未确认的自动候选不保存。`--query` 保留原有完整词条查询，`--sentence` 可独立验证整句结果。整句测试覆盖首选、手动改选、多音词读音约束、用户词复用、512 音节整句、歧义长输入性能和长句重启学习；真实 TSF 测试覆盖长句上屏、隐私文本框、密码/PIN 直接输入及取消不学习。

后续方向包括更多真实应用兼容性验证、词库和句子质量评测、上下文排序及代码签名；这些不代表当前已实现的能力。

自动英文识别使用本项目人工整理的常见英文词表，在空格、Enter 或标点确认首词时启动临时英文段；Enter 确认只提交原文，后续空格和字符直接交给应用，不保存句子或按键。小写单音节拼音及存储词库中的全拼、简拼、混输和末尾补全匹配优先，例如 `you/he/she/can` 仍按中文处理，`Hello/You/I` 等显式大写英文可以触发识别。未收录单词、拼写错误、人名和缩写不保证识别；这不是语言模型。数字选词、鼠标选词、候选导航及已选中文分段优先；可先手动切换中英文纠正歧义。网址/邮箱等特殊输入范围沿用原有规则，密码/PIN 直接输入。

TSF 接口设计参考微软 [Text Service Registration](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)、[Compositions](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions)、[RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)。


## 相关文档

- [输入指南](input-guide.md)：按键与命令行调试。
- [个人词库](personal-dictionaries.md)：导出、合并、学习及快照刷新。
- [Agent 接口](agent-dictionaries.md)：主题辅词库与 JSON CLI。
- [用户数据](user-data.md)：共享目录与旧数据迁移。
- [云同步](cloud-sync.md)：独立后台程序、网络协议及合并规则。
- [发布流程](releases.md)：版本、打包、升级与卸载。
