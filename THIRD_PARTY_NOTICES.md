# 词库第三方数据声明

自 2026-11-12 的常用词扩充起，`data/base.tsv` 的频率筛选扩充区及
`data/expansions/common-20261112.tsv` 包含下列 MIT 数据的派生内容。
原有词条、读音和权重保留；本次没有使用个人输入记录。

| 来源 | 使用内容 | 固定版本 | 许可原文 |
| --- | --- | --- | --- |
| [jieba](https://github.com/fxsjy/jieba) | `jieba/dict.txt` 的词语、词频和词性，用于选择新词与赋予排序权重 | [`67fa2e3`](https://github.com/fxsjy/jieba/tree/67fa2e36e72f69d9134b8a1037b83fbb070b9775) | [Sun Junyi，MIT](data/licenses/jieba-MIT.txt) |
| [phrase-pinyin-data](https://github.com/mozillazg/phrase-pinyin-data) | `pinyin.txt` 的整词读音，去声调并规范化 `ü → v` | [`4afa322`](https://github.com/mozillazg/phrase-pinyin-data/tree/4afa3226ce53c9138b939e83c3bb294dbe0da511) | [mozillazg，MIT](data/licenses/phrases-MIT.txt) |

版权分别归 Sun Junyi、mozillazg 等上游权利人所有；分发派生词库时须保留上表所链接的完整版权和许可文本。本项目自身许可见 [LICENSE](LICENSE)。

这里只使用 phrase-pinyin-data 的 `pinyin.txt`，没有导入它的 `large_pinyin.txt`、`cc_cedict.txt` 或汉典数据文件。其余新词仅在每个字在原库中都有唯一无调读音时生成读音；有读音歧义且没有唯一整词读音的候选跳过。既有词条不以外部读音覆盖。

筛选排除上游标记的人名、地名、机构名和其他专名（`nr/ns/nt/nz`）、纯数字组合、非汉字及未覆盖的字，按词频从高到低选取 30,000 个此前没有的多字词。上游词频只作为选择与排序信号，不代表当前口语中的精确频次。词性标签和读音仍可能有错误，需要持续根据实际使用修订。

维护脚本 [expand_common_dictionary.py](tools/expand_common_dictionary.py) 固定下载地址和 SHA-256，下载只发生在显式运行该维护命令时。来源、过滤参数、读音来源计数和输出哈希见 [来源清单](data/expansions/common-20261112.sources.json)。常规 C++ 构建和输入法运行使用已生成的本地 TSV，不需要 Python 或网络。
