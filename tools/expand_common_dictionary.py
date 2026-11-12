"""Rebuild the frequency-selected common vocabulary using pinned MIT data.

Python standard library only. Network access is limited to this maintenance
command; neither building nor running the IME downloads anything.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import unicodedata
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[1]
BEGIN = "# BEGIN frequency-selected common dictionary (2026-11-12)"
END = "# END frequency-selected common dictionary (2026-11-12)"
COUNT = 30000
SOURCES = {
    "jieba": {
        "repository": "fxsjy/jieba",
        "commit": "67fa2e36e72f69d9134b8a1037b83fbb070b9775",
        "files": {
            "jieba/dict.txt": "7197c3211ddd98962b036cdf40324d1ea2bfaa12bd028e68faa70111a88e12a8",
            "LICENSE": "18ba0984839f85853b29fadaf992f7dba8fd0ca0fbeae34de2b8735222dc7a37",
        },
    },
    "phrases": {
        "repository": "mozillazg/phrase-pinyin-data",
        "commit": "4afa3226ce53c9138b939e83c3bb294dbe0da511",
        "files": {
            "pinyin.txt": "5d4d86b1337bb2fca95bc5135564a821bfdb40597481ee3e3871bf52d4668622",
            "LICENSE": "89ac55df747e4776088c3e77531ef61b973a1a59dd8e6a4548a58996da9a4f70",
        },
    },
}


def source_files(cache, offline):
    result = {}
    for name, source in SOURCES.items():
        for filename, digest in source["files"].items():
            target = cache / name / Path(filename).name
            if not target.exists():
                if offline:
                    raise ValueError(f"missing source cache: {target}")
                url = f'https://raw.githubusercontent.com/{source["repository"]}/{source["commit"]}/{filename}'
                data = urlopen(url, timeout=45).read()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError(f"source digest mismatch: {url}")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            data = target.read_bytes()
            if hashlib.sha256(data).hexdigest() != digest:
                raise ValueError(f"source cache digest mismatch: {target}")
            result[name, filename] = data.decode("utf-8")
    return result


def toneless(value):
    # Decompose first: lǜ -> lu + diaeresis + tone -> lv, never lu.
    value = unicodedata.normalize("NFD", value.lower()).replace("u\u0308", "v")
    return " ".join("".join(c for c in value if not unicodedata.combining(c)).split())


def records(text):
    return [line.split("\t") for line in text.splitlines() if line and not line.startswith("#")]


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=ROOT / "out/dictionary-sources")
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    sources = source_files(args.cache, args.offline)
    base_path = ROOT / "data/base.tsv"
    base = base_path.read_text(encoding="utf-8")
    if BEGIN in base:
        before, _, tail = base.partition(BEGIN)
        _, separator, after = tail.partition(END)
        if not separator:
            raise ValueError("unterminated common dictionary block")
        base = before.rstrip() + "\n" + after.lstrip("\n")
    original = records(base)
    known = {r[0] for r in original}
    singles = defaultdict(set)
    for text, pronunciation, _ in original:
        if len(text) == 1:
            singles[text].add(pronunciation)
    phrases = defaultdict(set)
    for line in sources["phrases", "pinyin.txt"].splitlines():
        if not line or line.startswith("#"):
            continue
        word, value = line.split(":", 1)
        pronunciation = toneless(value.split("#", 1)[0])
        if len(word) == len(pronunciation.split()) and re.fullmatch(r"[a-z]+(?: [a-z]+)*", pronunciation):
            phrases[word].add(pronunciation)
    candidates = {}
    for line in sources["jieba", "jieba/dict.txt"].splitlines():
        word, frequency, pos = line.split()
        frequency = int(frequency)
        if (word in known or frequency < 20 or not 2 <= len(word) <= 8
                or not all("\u4e00" <= c <= "\u9fff" and c in singles for c in word)
                or pos.startswith(("nr", "ns", "nt", "nz"))
                or re.fullmatch("[零〇一二两三四五六七八九十百千万亿点]+", word)):
            continue
        if len(phrases[word]) == 1:
            pronunciation = next(iter(phrases[word]))
            method = "phrase-pinyin-data/pinyin.txt"
        elif not phrases[word] and all(len(singles[c]) == 1 for c in word):
            pronunciation = " ".join(next(iter(singles[c])) for c in word)
            method = "existing-unambiguous-characters"
        else:
            continue  # Never guess a polyphone's first reading or form products.
        item = (frequency, word, pronunciation, method)
        if word not in candidates or frequency > candidates[word][0]:
            candidates[word] = item
    selected = sorted(candidates.values(), key=lambda row: (-row[0], row[1]))[:COUNT]
    if len(selected) != COUNT:
        raise ValueError(f"only {len(selected)} eligible common words")
    additions = set()
    rows = []
    for frequency, word, pronunciation, _ in selected:
        weight = min(5500, 1000 + int(400 * math.log1p(frequency)))
        rows.append(f"{word}\t{pronunciation}\t{weight}\n")
        for character, reading in zip(word, pronunciation.split()):
            if reading not in singles[character]:
                additions.add((character, reading))
    rows += ["# 补齐本批词语所需的单字读音\n"]
    rows += [f"{character}\t{reading}\t3500\n" for character, reading in sorted(additions)]
    content = ("# Frequency-selected common vocabulary, 2026-11-12.\n"
               "# Derived from jieba (MIT) and phrase-pinyin-data/pinyin.txt (MIT).\n"
               "# Sources, filters and licenses: ../../THIRD_PARTY_NOTICES.md\n" + "".join(rows))
    directory = ROOT / "data/expansions"
    write(directory / "common-20261112.tsv", content)
    metadata = {
        "sources": SOURCES,
        "selection": {"new_multi_character_texts": COUNT, "minimum_source_frequency": selected[-1][0],
                      "word_length": [2, 8], "excluded_pos_prefixes": ["nr", "ns", "nt", "nz"],
                      "minimum_candidate_frequency": 20, "previous_readings_preserved": True},
        "readings": dict(Counter(row[3] for row in selected)),
        "supplemental_character_readings": len(additions),
        "output_sha256": hashlib.sha256(content.encode("utf-8")).hexdigest(),
    }
    write(directory / "common-20261112.sources.json", json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
    for name in SOURCES:
        write(ROOT / "data/licenses" / f"{name}-MIT.txt", sources[name, "LICENSE"])
    base = base.replace("# Generated in this conversation; no third-party word list was imported.",
                        "# Original sections curated for this project; imported expansion attributed in THIRD_PARTY_NOTICES.md.")
    write(base_path, base.rstrip() + "\n\n" + BEGIN + "\n" + content + END + "\n")
    print(json.dumps(metadata, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
