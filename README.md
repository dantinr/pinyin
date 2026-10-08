# Private Pinyin：Windows 中文拼音词库原型

使用 C++17 编写的本地词库和候选查询核心，以及命令行演示程序。**目前不是可安装的 Windows 输入法**：尚未实现 TSF、组合文本和候选窗口，也不能向其他应用输入文字。

## 已实现

- 按音节建立 Trie，查询连续全拼，例如 `nihao`、`chongqing`。
- 保留歧义切分：`xian` 可以匹配“先”和“西安”；`xi'an` 仅匹配后者。
- 处理大小写、空格和 `ü → v`；输入和词库不使用声调。
- 词级多音字读音；同词不同读音保留，同词同读音取较大权重。
- 基于词权重的候选排序；可选的本地选词次数加分，设有上限。
- 可选自造词、用户词库加载和清空；同目录临时文件加原子替换，使用 Windows 独占文件锁避免多进程覆盖更新。
- 严格校验 TSV、音节、UTF-8、权重，报错包含文件行号；基础词库加载失败不影响原有数据。
- 基础词库路径默认相对于可执行文件，支持中文目录和中文命令行参数。

## 构建与测试

安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载和 CMake。在开发者 PowerShell 中运行：

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

核心测试覆盖拼音歧义、多音字、排序、损坏词库和原子存储。配置时检测到 PowerShell 7（`pwsh`）还会加入 CLI 集成测试，检查默认无痕不创建用户目录、显式学习、自造词、重启加载和清空；测试使用独立临时目录，不访问真实用户词库。

对于 Visual Studio 多配置生成器，可执行文件位于 `build\Release\private_pinyin.exe`，构建时自动复制示例词库。其他生成器的可执行文件位置可能不同。

```powershell
.\build\Release\private_pinyin.exe --query nihao
.\build\Release\private_pinyin.exe --query "xi'an"
.\build\Release\private_pinyin.exe
.\build\Release\private_pinyin.exe --learn
```

## 使用与隐私

默认不读取、创建或写入用户词库，不学习。程序没有网络调用、第三方依赖、输入日志、遥测或剪贴板访问。

显式传入 `--learn` 后，在 `%LOCALAPPDATA%\PrivatePinyin\words.user.tsv` 保存“词语、拼音、选择次数”。可用 `--user 路径` 指定位置。数据为本地明文，不提供静态加密，也不能防止其他有权限访问文件的本机程序读取它。锁文件仅用于并发控制，不含输入记录。

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

`include/pinyin/lexicon.hpp` 是词库接口，`src/lexicon.cpp` 实现解析、索引和排序；`src/user_store.cpp` 负责 Windows 本地存储；`src/main.cpp` 是演示入口。

当前匹配完整词条，不支持未完成音节补全、简拼、模糊音、纠错、双拼或自动整句组合。用户只输入 `niha` 不会得到“你好”；“我在北京”必须是一个词条才会整体匹配。排名算法是原型，并非训练过的语言模型；用户加分有上限，暂未实现时间衰减。单字多音字表不能自动推导正确的多音词读音，需要词条显式标注。

建议后续依次完成：词库导入和质量评测、拼音切分图与整句解码、Windows TSF 前端、候选窗口、应用兼容性、安装签名和升级。
