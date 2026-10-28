"""Merge maintained theme sources into base.tsv using only the Python standard library.

Existing records outside the managed block keep their weights. Runtime loading
still uses base.tsv; the IME does not need Python or read these source files.
"""
from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "data" / "base.tsv"
THEMES = (("animals", "动物"), ("plants", "植物"), ("movies", "电影"))
BEGIN = "# BEGIN maintained theme dictionaries"
END = "# END maintained theme dictionaries"


def read_rows(content, source):
    rows = []
    seen = set()
    for number, line in enumerate(content.splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        columns = line.split("\t")
        if len(columns) != 3:
            raise ValueError(f"{source}:{number}: expected three tab-separated columns")
        text, pinyin, weight = columns
        if not text or not re.fullmatch(r"[a-z]+(?: [a-z]+)*", pinyin):
            raise ValueError(f"{source}:{number}: noncanonical text or pinyin")
        if len(text) != len(pinyin.split(" ")):
            raise ValueError(f"{source}:{number}: character/syllable count mismatch: {text}")
        if not weight.isascii() or not weight.isdigit() or not 0 < int(weight) <= 1000000000:
            raise ValueError(f"{source}:{number}: invalid weight")
        key = (text, pinyin)
        if key in seen:
            raise ValueError(f"{source}:{number}: duplicate word/reading: {text}")
        seen.add(key)
        rows.append((text, pinyin, weight))
    return rows


def merge():
    original = BASE.read_text(encoding="utf-8")
    # Remove only our bounded block; preserve unrelated additions after it.
    lines = original.splitlines(keepends=True)
    starts = [i for i, line in enumerate(lines) if line.rstrip("\r\n") == BEGIN]
    ends = [i for i, line in enumerate(lines) if line.rstrip("\r\n") == END]
    if starts or ends:
        if len(starts) != 1 or len(ends) != 1 or starts[0] >= ends[0]:
            raise ValueError("base.tsv: missing, repeated or reversed theme block markers")
        del lines[starts[0]:ends[0] + 1]
    retained = "".join(lines).rstrip("\n")
    existing = read_rows(retained, "base.tsv")
    all_rows = existing.copy()
    seen = {(text, pinyin) for text, pinyin, _ in existing}
    block = [BEGIN, "# 主题词库扩充（2026-10-28）；由 tools/merge_theme_dictionaries.py 维护。"]
    counts = {}
    for name, title in THEMES:
        source = ROOT / "data" / "themes" / f"{name}.tsv"
        rows = read_rows(source.read_text(encoding="utf-8"), source.name)
        block.append(f"# {title}主题：data/themes/{name}.tsv")
        added = 0
        for row in rows:
            key = row[:2]
            if key not in seen:
                seen.add(key)
                all_rows.append(row)
                block.append("\t".join(row))
                added += 1
        counts[name] = {"source_records": len(rows), "added_records": added}

    single_readings = {(text, pinyin) for text, pinyin, _ in all_rows if len(text) == 1}
    missing = set()
    for text, pinyin, _ in all_rows:
        for pair in zip(text, pinyin.split(" ")):
            if pair not in single_readings:
                missing.add(pair)
    block.append("# 主题词语所需补充单字读音")
    for text, pinyin in sorted(missing):
        row = (text, pinyin, "4500")
        all_rows.append(row)
        block.append("\t".join(row))
    block.append(END)
    merged = retained + "\n\n" + "\n".join(block) + "\n"
    # Audit the final file before replacing it, including retained records.
    read_rows(merged, "base.tsv")
    if merged != original:
        BASE.write_text(merged, encoding="utf-8", newline="\n")
    singles = [row for row in all_rows if len(row[0]) == 1]
    words = [row for row in all_rows if len(row[0]) > 1]
    print(json.dumps({
        "themes": counts,
        "added_single_readings": len(missing),
        "total_records": len(all_rows),
        "distinct_texts": len({row[0] for row in all_rows}),
        "distinct_characters": len({row[0] for row in singles}),
        "single_readings": len(singles),
        "distinct_words": len({row[0] for row in words}),
        "word_readings": len(words),
        "bytes": BASE.stat().st_size,
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    merge()
