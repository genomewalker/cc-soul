#!/usr/bin/env python3
"""Export constellation.json → docs/constellation-data.json (web-safe, no direction vectors).

Run after each constellation rebuild:
  python3 scripts/export_constellation_web.py \
    --input /path/to/constellation.json \
    --output docs/constellation-data.json
"""
import argparse, json, re, sys
from pathlib import Path

# Regex for detecting mixed Unicode scripts within a single token —
# almost always a byte-level BPE decoding artifact, not real content.
_MIXED_CJK_LATIN = re.compile(
    r"(?:[⺀-鿿豈-﫿぀-ヿ가-힯].*[a-zA-Z]"
    r"|[a-zA-Z].*[⺀-鿿豈-﫿぀-ヿ가-힯])"
)
_MIXED_ARABIC_OTHER = re.compile(
    r"(?:[؀-ۿݐ-ݿﭐ-﷿ﹰ-﻿].*[a-zA-Z一-鿿]"
    r"|[a-zA-Z一-鿿].*[؀-ۿݐ-ݿﭐ-﷿ﹰ-﻿])"
)
# Unusual Latin diacritics (IPA/phonetic range) appearing before plain ASCII words
_UNUSUAL_LATIN_PREFIX = re.compile(r"^[ɐ-ʰḀ-ỿ][a-zA-Z]{3,}")


def _fix_mojibake(t: str) -> str:
    """Repair UTF-8 bytes that were decoded as Latin-1 (double-encoding).

    Heuristic: if the string encodes losslessly to Latin-1 bytes and those
    bytes decode as valid UTF-8, the original text was UTF-8 that got
    misread as Latin-1. This correctly fixes 'Ãºltimos' → 'últimos' and
    double-encoded CJK/Thai/etc. while leaving pure ASCII tokens untouched.
    """
    try:
        fixed = t.encode("latin-1").decode("utf-8")
        return fixed
    except (UnicodeEncodeError, UnicodeDecodeError):
        return t


def clean_token(t: str) -> str:
    """Strip BPE/SentencePiece prefix chars, repair mojibake, normalize."""
    t = t.lstrip("Ġ▁Ċ").strip()
    t = re.sub(r"[\x00-\x1f\x7f]", "", t)
    t = _fix_mojibake(t)
    return t


def _is_artifact(t: str) -> bool:
    """Return True for tokens that are tokenizer byte-decoding artifacts."""
    if _MIXED_CJK_LATIN.search(t):
        return True
    if _MIXED_ARABIC_OTHER.search(t):
        return True
    if _UNUSUAL_LATIN_PREFIX.match(t):
        return True
    # Byte-continuation markers: Latin Extended-A/B char before plain ASCII word
    # e.g. ĉmysql, ĉprintf (U+0109 'c with circumflex' + identifier)
    if re.match(r"^[Ā-ɏ][a-zA-Z_][a-zA-Z0-9_]{2,}", t):
        return True
    return False


_DENSE_SUPPLEMENT = re.compile(r"[\x80-\xffĀ-ɏ]{3,}")


def _display_score(t: str) -> int:
    """Lower = better display priority. Prefer readable Latin/ASCII words."""
    if not t or len(t) < 2 or len(t) > 35:
        return 1000
    if _is_artifact(t):
        return 999
    # Dense run of Latin-Supplement/Extended chars: likely still-broken mojibake
    # (e.g. Thai via byte-fallback mapping → can't repair with latin-1 roundtrip)
    if _DENSE_SUPPLEMENT.search(t) and not re.search(r"[A-Za-z]{3,}", t):
        return 800
    # Clean ASCII alphabetic word/phrase
    if re.match(r"^[A-Za-z][A-Za-z0-9 '\-]{1,}$", t):
        return 0
    # Latin with common European diacritics (French, German, Spanish…)
    if re.match(r"^[À-ɏA-z][^̀-ͯ\x80-\xff]{1,}$", t):
        return 10
    # Starts with punctuation / operator / path separator — code token
    if t[0] in "./\\#@$*+<>(){}[]=":
        return 300
    # CJK, Thai, Arabic, Hangul, Cyrillic etc. (valid multilingual, low label priority)
    if re.search(r"[฀-๿؀-ۿ⺀-鿿가-힯Ѐ-ӿऀ-ൿႠ-ჿ]", t):
        return 150
    return 50


def _rank_tokens(raw: list[str]) -> list[str]:
    """Clean, deduplicate, filter artifacts, sort by display priority."""
    seen: set[str] = set()
    scored: list[tuple[int, str]] = []
    for t in raw:
        t = clean_token(t)
        if not t or len(t) < 2 or t in seen:
            continue
        score = _display_score(t)
        if score >= 999:   # artifact — drop entirely
            continue
        seen.add(t)
        scored.append((score, t))
    scored.sort(key=lambda x: x[0])
    return [t for _, t in scored]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--input", default="constellation.json")
    p.add_argument("--output", default="docs/constellation-data.json")
    args = p.parse_args()

    with open(args.input) as f:
        c = json.load(f)

    dirs = []
    for d in c.get("directions", []):
        ranked = _rank_tokens(d["top_tokens"][:24])[:12]
        if not ranked:
            continue
        dirs.append({
            "id":               d["feature_id"],
            "tokens":           ranked,
            "consensus":        round(d.get("consensus_score", 0), 3),
            "n_models":         d.get("n_supporting_models", 1),
            "models":           d.get("supporting_models", []),
            "provenance":       d.get("provenance", "ow_distilled"),
        })

    out = {
        "n_directions":   len(dirs),
        "source_models":  c.get("source_models", []),
        "n_input_models": c.get("n_input_models", 0),
        "directions":     dirs,
    }
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(out, f, separators=(",", ":"))

    kb = Path(args.output).stat().st_size // 1024
    print(f"Exported {len(dirs)} directions → {args.output} ({kb}KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
