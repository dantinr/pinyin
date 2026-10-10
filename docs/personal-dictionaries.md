# 个人词库导出与合并

0.9.0 起，右键语言栏或任务栏的“中 / 英”按钮，打开“设置…”，可使用“导出个人词库…”和“合并词库…”。也可直接打开安装目录中的 `private_pinyin_settings.exe`。

## 导出

点击“导出个人词库…”，选择保存位置。导出包含已学习的词语、完整拼音和选择次数，使用 UTF-8 TSV，支持中文文件名。不导出基础词库、辅词库、设置或输入原文。覆盖已有导出文件时，文件选择窗口会提示确认。尚未学习任何词语时可导出空词库。

## 合并

点击“合并词库…”，在文件窗口底部选择对应的“文件类型”：

- **个人词库（选择次数）**：将另一份个人词库合并到 `%USERPROFILE%\PrivatePinyin\words.user.tsv`。同词同读音的选择次数取较大值，不相加；不同读音保留。重复导入相同文件不改变词库，也不产生额外备份。
- **普通词库（排序权重）**：将普通三列 TSV 合并到 `%USERPROFILE%\PrivatePinyin\dictionaries\imported.tsv` 辅词库。同词同读音取较大权重，不修改主词库；辅词库不依赖本地学习开关。其他主题辅词库仍可通过 [Agent CLI](agent-dictionaries.md) 分别管理。若 `imported` 先前被 Agent 停用，合并保留停用状态，并在结果中提示。

个人词库合并有实际变更且原文件已存在时，先把原文件完整备份到同目录 `backups` 文件夹，名称包含时间与唯一编号。结果窗口和 CLI 返回此次备份路径。备份失败、格式错误、写入锁超时或文件替换失败时不覆盖现有词库。备份也是个人词库，可再次选择合并；合并用于补充，不用于撤销或降低现有选择次数。

个人词库在开启本地学习时参与候选排序；合并不会改变学习开关。下一轮拼音输入即可使用新增词语，正在编辑的组合不会中途更改。关闭学习时可照常导出、合并保存的数据。

## TSV 格式

个人词库第三列为选择次数，普通词库第三列为排序权重。三列均使用实际 TAB 分隔：

```text
# private-pinyin user dictionary v1: text<TAB>pinyin<TAB>selection_count
电脑	dian nao	5
重载	chong zai	2
重载	zhong zai	7
```

文件为 UTF-8，可带 BOM，支持 LF/CRLF 和以 `#` 开头的注释。拼音不带声调，音节用空格分隔，`ü` 用 `v`；拼音会规范化。次数为 1 到 1,000,000,000 的整数。导入文件允许重复行，规范化后取较大次数；当前活动个人词库仍严格拒绝重复行，避免静默覆盖手工损坏的数据。空文件表示没有新增记录。

导出的个人词库带上述类型标记。系统能拒绝把本项目带主词库或辅词库标记的文件当成个人选择次数；没有类型注释的 TSV 以用户所选文件类型为准，手工整理时应保留正确标记。

## 命令行与 Agent

`personal` 是显式访问个人词库的独立入口；原有 `lexicon` 入口仍只管理主词库与辅词库。输出一行 UTF-8 JSON，`schema=1`，成功退出码 0、失败 1。可以先运行 `personal help` 发现命令。

```powershell
.\private_pinyin.exe personal export --file "D:\备份\个人词库.tsv"
.\private_pinyin.exe personal validate --file "D:\备份\个人词库.tsv"
.\private_pinyin.exe personal merge --file "D:\备份\另一份个人词库.tsv"
```

`export` 默认拒绝覆盖已有文件；明确覆盖时加 `--overwrite`。禁止导出覆盖活动个人词库自身，包括指向同一文件的链接。`export` 和 `merge` 可用 `--user "D:\测试\words.user.tsv"` 指定独立个人词库；不指定则使用当前 Windows 用户的默认路径。`validate` 只检查源文件，不访问活动个人词库。

合并结果包含 `imported`（导入去重后条数）、`added`、`updated`、`unchanged`、`total` 和 `backup`；没有备份时 `backup` 为空字符串。导出结果包含 `entries` 和保存 `path`；校验结果包含 `records`、`entries`、`duplicates`。

导出读取完整已发布快照，不锁住其他应用的学习；合并持有短暂写入锁，重新读取最新记录后保存，避免丢失其他应用刚学习的词语。不会联网或自动上传导出文件。
