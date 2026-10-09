隐私拼音：Windows x64 离线安装包

安装
1. 将 ZIP 完整解压到任意目录。不要直接从压缩包内运行程序。
2. 双击 install.cmd，并同意 Windows 管理员权限提示。
3. 安装器将程序复制到 C:\Program Files\PrivatePinyin\版本号，
   必要时安装随包附带的微软 VC++ x64 运行库，再注册输入法；全过程无需联网。
4. 使用 Win+Space 切换到“隐私拼音（开发版）”，重新打开需要输入的应用。
5. 输入 nihao，按空格选“你好”；数字 1–9 选词，PgUp/PgDn 翻页。
   也可用简拼 nh 或混输 nhao/nih；bj 可选“北京”，zhg/zg 可选“中国”。
   简拼支持分段改选、整句组词和本地学习，学习时保存完整读音。

默认开启本地学习。词语、读音和选择次数保存在
%LOCALAPPDATA%\PrivatePinyin\words.user.tsv。
密码/PIN 框直接输入。没有联网、遥测、云词库或原文按键日志。
学习开关可通过安装目录中的 private_pinyin.exe --ime-learning on|off|status|clear 设置。

安装完成后，下载的 ZIP 和解压目录可以删除。
安装目录必须保留。每个新版本安装到独立目录，更新不覆盖已加载的旧 DLL。
升级后重新打开应用以加载新版本；旧版本目录可在应用退出后手动删除。

卸载
在当前安装版本的目录内双击 uninstall.cmd。
卸载只移除本输入法注册，保留个人词库和共用的微软 VC++ 运行库。
关闭加载输入法的应用后，可手动删除该版本安装目录。

private_pinyin_demo.exe 是无需系统注册的测试窗口；运行前需已安装随包的 VC++ 运行库。
private_pinyin.exe 是词库查询和本地学习管理工具。
data\README.md 说明基础词库的来源和覆盖范围。
manifest.json 记录版本、来源提交、运行库版本和文件 SHA-256，用于完整性检查。

当前为开发预览版，仅支持 Windows x64 桌面应用，尚未做代码签名。
系统框架由 Windows 提供；vc_redist.x64.exe 是微软官方运行库安装包。
