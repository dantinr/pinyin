# 共享个人数据与旧目录迁移

当前默认目录为 `%USERPROFILE%\PrivatePinyin`，例如 `C:\Users\用户名\PrivatePinyin`。输入法、设置程序和 Agent CLI 共用这一目录：

| 文件或目录 | 用途 |
| --- | --- |
| `words.user.tsv` | 已确认的个人词语及选择次数 |
| `words.user.tsv.*.disabled` | 本地学习、中文标点、自动英文的关闭标记 |
| `dictionaries\*.tsv` | 用户或 Agent 管理的辅词库 |
| `dictionaries\*.tsv.disabled` | 辅词库的停用标记 |
| `backups` | 个人词库合并前的备份 |
| `migration` | 旧文件的迁移快照、完成记录和错误记录 |

旧版默认目录为 `%LOCALAPPDATA%\PrivatePinyin`。Windows 的 MSIX AppData 虚拟化可能让同一逻辑路径对应不同内容：普通应用访问共享原件，而商店应用访问 `%LOCALAPPDATA%\Packages\包族名称\LocalCache\Local\PrivatePinyin` 中的私有副本。因此，文件查看器中有记录，也不代表另一个应用里的旧版输入法能读到它。当前默认目录位于 AppData 之外。[微软的虚拟化说明](https://learn.microsoft.com/en-us/windows/msix/desktop/flexible-virtualization)解释了私有副本的读取优先级与用户目录其他位置的行为。

输入法激活或工具使用默认目录时，自动发现旧共享目录和上述包目录中的 PrivatePinyin 数据。每份实际打开的个人词库只迁移一次；同词同读音的次数取较大值，不相加，不降低新目录中的已有次数。迁移只读取固定名称 `words.user.tsv`，不会把旧目录中其他 TSV 或备份文件自动当作个人词库。辅词库仍仅迁移 `dictionaries` 下符合命名规则的 TSV，合并权重取较大值。

首次迁移沿用当前应用看到的旧设置；新目录已有个人词库、设置或辅词库时，保留新设置。同名辅词库已存在时保留其启用状态；首次迁入的辅词库保留旧停用标记。旧文件和旧备份保留在原处，迁移快照另存于新目录的 `migration`。完成记录按实际文件路径区分共享原件和私有副本，后续不会重新导入已迁移文件，避免撤销清空词库或 Agent 的删除操作。

更新 DLL、设置程序与 CLI 后，重新打开正在使用输入法的应用。仍加载旧 DLL 的应用会继续写旧目录；若旧目录在已完成迁移后又有新增词语，可用“合并词库…”或 `personal merge --file "旧文件路径"` 显式合并。尚未迁移过的共享原件会在能访问它的普通应用启动新输入法后迁移。工具的显式 `--user`、`--root` 路径保持用户指定的位置，不参与默认目录迁移。

损坏的旧文件不覆盖新词库，其他有效文件仍可迁移。查看 `migration\errors.txt` 获取待修复的源文件路径；修复后重新激活输入法或打开设置程序即可重试。目录访问失败或迁移锁繁忙时保留旧文件，下一次激活重试。旧副本及迁移快照不会随清空个人词库一起删除。
