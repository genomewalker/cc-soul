#!/usr/bin/env python3
"""
gen_hint_corpus_ssl.py — Build hint extraction training corpus from real transcripts.

SSL signal: when Claude called mcp__chitta__remember during a session, the last real
user message before that call is a ground-truth positive. Pairs are extracted without
any LLM labeling — the soul memory IS the label (converted to natural language by the
existing Ollama/codex labeler pipeline).

User turns with no nearby remember call become negatives (labeled empty by the labeler).

Usage:
    python3 gen_hint_corpus_ssl.py \\
        [--projects-dir ~/.claude/projects] \\
        [--out /scratch/hint_corpus_ssl_chatml.jsonl] \\
        [--max-sessions 500] \\
        [--neg-ratio 2.0] \\
        [--window 25] \\
        [--ollama-url http://dandygpun01fl:11434] \\
        [--ollama-model qwen3.6:27b] \\
        [--dry-run]
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterator

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

DEFAULT_PROJECTS_DIR = os.path.expanduser("~/.claude/projects")
DEFAULT_OUT = "/maps/projects/caeg/scratch/kbd606/tmp/hint_corpus_ssl_chatml.jsonl"
OLLAMA_URL  = "http://dandygpun01fl:11434"

SYSTEM_PROMPT = (
    "Extract a single concise factual hint from the user message. "
    "Cover: personal preferences, tech choices, developer workflows, domain expertise, "
    "project facts, and recurring patterns. "
    "If nothing factual is present, output nothing."
)

# Patterns that mark noise user turns (skip these as inputs)
NOISE_PREFIXES = (
    "Base directory",
    "This session is being continued",
    "[Request interrupted",
    "<local-command",
    "<command-name",
    "<system-reminder>",
    "<task-notification>",
    "You are **claude:",       # room participant preamble
)

# Remember calls carrying these prefixes are job/tracking records, not personal facts
JOB_PREFIXES = ("[job]", "[task]", "[agent-task]", "[thread]",
                "[OPERATIONAL]", "[PROBE]", "Phase ", "phase ")

# Max chars for a remembered content to be usable as a hint label
MAX_REMEMBER_CHARS = 400
MIN_REMEMBER_CHARS = 20

# Min user text length for pairing (short "what is next?" messages are noise)
MIN_USER_TEXT_LEN = 30

# Min overlap (fraction of memory keywords found in user text) to accept a pair
MIN_OVERLAP = 0.15

# ---------------------------------------------------------------------------
# Transcript parsing
# ---------------------------------------------------------------------------

def _extract_text_blocks(content) -> list[str]:
    if isinstance(content, str):
        return [content] if content.strip() else []
    if isinstance(content, list):
        out = []
        for block in content:
            if isinstance(block, dict) and block.get("type") == "text":
                t = block.get("text", "").strip()
                if t:
                    out.append(t)
        return out
    return []
def _is_noise(text: str) -> bool:
    for p in NOISE_PREFIXES:
        if text.startswith(p):
            return True
    if len(text) < 8:
        return True
    # Skill invocations / system context injections (long with code/markdown structure)
    if text.count("\n") > 6 and (
        "<|im_start|>" in text
        or "## Session" in text
        or "```" in text
        or "# /loop" in text
        or "Parse the input below" in text
        or "UserPromptSubmit says:" in text
    ):
        return True
    return False
def _extract_remember_content(tool_input: dict) -> str | None:
    content = tool_input.get("content", "").strip()
    if not content:
        return None
    if len(content) < MIN_REMEMBER_CHARS or len(content) > MAX_REMEMBER_CHARS:
        return None
    if content.startswith(JOB_PREFIXES):
        return None
    return content


def parse_transcript(path: Path) -> list[dict]:
    """
    Parse a Claude Code JSONL transcript into a flat list of events:
      {"role": "user"|"assistant", "text": str, "ts": str}
      {"role": "remember", "content": str, "ts": str}
    """
    events = []
    for line in path.open():
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            continue

        ts = d.get("timestamp", "")[:19]
        msg = d.get("message", {})
        role = msg.get("role", "")
        content = msg.get("content", [])

        if role == "user":
            texts = _extract_text_blocks(content)
            for text in texts:
                if not _is_noise(text):
                    events.append({"role": "user", "text": text, "ts": ts})

        elif role == "assistant":
            # Scan for remember/learn tool_use blocks
            if isinstance(content, list):
                for block in content:
                    if not isinstance(block, dict):
                        continue
                    if block.get("type") != "tool_use":
                        continue
                    name = block.get("name", "")
                    if "remember" not in name and "learn" not in name:
                        continue
                    inp = block.get("input", {})
                    mem = _extract_remember_content(inp)
                    if mem:
                        events.append({"role": "remember", "content": mem, "ts": ts})

    return events


def _keyword_overlap(user_text: str, mem_content: str) -> float:
    """Fraction of significant memory words that appear in the user text."""
    stopwords = {"the","a","an","is","are","was","were","to","of","in","on","for",
                 "and","or","not","with","at","by","as","it","be","this","that",
                 "from","have","has","had","can","do","use","get","set","run","add",
                 "will","we","i","you","my","your","our","its","into","about","if"}
    def tokens(text: str) -> set[str]:
        return {w.lower() for w in re.findall(r"[a-zA-Z]{3,}", text) if w.lower() not in stopwords}
    mem_toks = tokens(mem_content)
    if not mem_toks:
        return 0.0
    user_toks = tokens(user_text)
    return len(mem_toks & user_toks) / len(mem_toks)


def extract_pairs(events: list[dict], window: int) -> tuple[list[tuple[str, str]], list[str]]:
    """
    Returns (positives, negatives):
      positives: list of (user_text, remember_content)
      negatives: list of user_text (no remember within `window` events)

    A pair is only accepted if:
      - user_text >= MIN_USER_TEXT_LEN chars
      - keyword overlap between user_text and memory content >= MIN_OVERLAP
    """
    positives: list[tuple[str, str]] = []
    used_user_indices: set[int] = set()

    for j, ev in enumerate(events):
        if ev["role"] != "remember":
            continue
        # Find the best-matching user turn within `window` events before this remember
        best_k = best_overlap = -1.0
        for k in range(j - 1, max(j - window, -1), -1):
            if events[k]["role"] != "user":
                continue
            if k in used_user_indices:
                continue
            utext = events[k]["text"]
            if len(utext) < MIN_USER_TEXT_LEN:
                continue
            overlap = _keyword_overlap(utext, ev["content"])
            if overlap > best_overlap:
                best_overlap = overlap
                best_k = k

        if best_k >= 0 and best_overlap >= MIN_OVERLAP:
            positives.append((events[best_k]["text"], ev["content"]))
            used_user_indices.add(best_k)

    negatives: list[str] = []
    for k, ev in enumerate(events):
        if ev["role"] == "user" and k not in used_user_indices:
            if len(ev["text"]) >= MIN_USER_TEXT_LEN:
                negatives.append(ev["text"])

    return positives, negatives


# ---------------------------------------------------------------------------
# SSL notation → natural language hint via Ollama labeler
# ---------------------------------------------------------------------------

CONVERT_INSTRUCTIONS = """You are converting soul memory SSL notation into natural-language retrieval hints.

SSL notation uses arrow syntax: "subject → predicate | alternative" and brackets: [kind] [domain].

For each input, output a concise hint (8–25 words, third person, present tense) that captures
the core fact. If the notation is a job-tracking record, system message, or has no factual
content about the user, output an empty string "".

Output ONLY a JSON array of strings — one per input, in order. No markdown, no explanation."""
def ssl_to_hint_batch(memories: list[str], ollama_url: str, ollama_model: str) -> list[str]:
    """Convert SSL notation memories to natural language hints, one at a time."""
    import urllib.request as _urllib
    system = (
        "Convert this compact memory notation into a single natural language retrieval hint. "
        "Output one concise factual sentence in third person. "
        "Preserve all technical details (paths, versions, names). "
        "Output nothing if it is a job/task/operational record."
    )
    results = []
    for i, mem in enumerate(memories):
        body = json.dumps({
            "model": ollama_model,
            "prompt": mem[:500],
            "system": system,
            "stream": False,
            "think": False,
            "options": {"temperature": 0.1, "num_predict": 80},
        }).encode()
        req = _urllib.Request(
            f"{ollama_url}/api/generate",
            data=body,
            headers={"Content-Type": "application/json"},
        )
        try:
            with _urllib.urlopen(req, timeout=120) as r:
                resp = json.loads(r.read())
            text = resp.get("response", "").strip()
            # trim to first sentence
            for sep in (".", "\n"):
                idx = text.find(sep)
                if 0 < idx < 150:
                    text = text[:idx + 1].strip()
                    break
            results.append(text[:150] if len(text) >= 6 else "")
        except Exception as e:
            sys.stderr.write(f"[ssl-convert] item {i} ollama failed: {e}\n")
            results.append("")
    return results
def label_negatives_batch(turns: list[str], ollama_url: str, ollama_model: str) -> list[str]:
    """Label user turns one at a time — output hint if factual, empty if not."""
    import urllib.request as _urllib
    system = (
        "Extract a single concise retrieval hint from the user message. "
        "Cover: personal preferences, tech choices, developer workflows, domain expertise, project facts. "
        "If nothing factual is present, output nothing."
    )
    results = []
    for i, turn in enumerate(turns):
        body = json.dumps({
            "model": ollama_model,
            "prompt": turn[:400],
            "system": system,
            "stream": False,
            "think": False,
            "options": {"temperature": 0.1, "num_predict": 80},
        }).encode()
        req = _urllib.Request(
            f"{ollama_url}/api/generate",
            data=body,
            headers={"Content-Type": "application/json"},
        )
        try:
            with _urllib.urlopen(req, timeout=120) as r:
                resp = json.loads(r.read())
            text = resp.get("response", "").strip()
            if not text or text.startswith("-") or len(text) < 5:
                results.append("")
            else:
                for sep in (".", "\n"):
                    idx = text.find(sep)
                    if 0 < idx < 150:
                        text = text[:idx + 1].strip()
                        break
                results.append(text[:150])
        except Exception as e:
            sys.stderr.write(f"[neg-label] item {i} ollama failed: {e}\n")
            results.append("")
    return results
def _parse_array(text: str, expected: int) -> list[str]:
    text = re.sub(r"^```[^\n]*\n?", "", text)
    text = re.sub(r"\n?```$", "", text.rstrip())
    try:
        arr = json.loads(text)
        if isinstance(arr, list):
            arr = arr[:expected] + [""] * max(0, expected - len(arr))
            return [str(h).strip() for h in arr]
    except json.JSONDecodeError:
        pass
    m = re.search(r"\[.*\]", text, re.DOTALL)
    if m:
        try:
            arr = json.loads(m.group(0))
            if isinstance(arr, list):
                arr = arr[:expected] + [""] * max(0, expected - len(arr))
                return [str(h).strip() for h in arr]
        except json.JSONDecodeError:
            pass
    sys.stderr.write(f"[parse] could not parse JSON array from response\n")
    return [""] * expected


# ---------------------------------------------------------------------------
# ChatML formatting
# ---------------------------------------------------------------------------

def make_example(user_text: str, hint: str) -> dict:
    return {
        "conversations": [
            {"from": "system",  "value": SYSTEM_PROMPT},
            {"from": "human",   "value": user_text},
            {"from": "gpt",     "value": hint},
        ]
    }


# ---------------------------------------------------------------------------
# Session discovery
# ---------------------------------------------------------------------------

def discover_sessions(projects_dir: str, max_sessions: int) -> list[Path]:
    root = Path(projects_dir)
    sessions = []
    for jsonl in sorted(root.rglob("*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True):
        if jsonl.stat().st_size < 10_000:
            continue
        sessions.append(jsonl)
        if len(sessions) >= max_sessions:
            break
    sys.stderr.write(f"[discover] {len(sessions)} sessions (≥10KB) in {projects_dir}\n")
    return sessions


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run(projects_dir: str, out_path: str, max_sessions: int, neg_ratio: float,
        window: int, ollama_url: str, ollama_model: str, batch_size: int,
        dry_run: bool) -> None:

    sessions = discover_sessions(projects_dir, max_sessions)

    # Phase 1: extract all pairs
    all_positives: list[tuple[str, str]] = []
    all_negatives: list[str] = []

    for i, path in enumerate(sessions):
        sys.stderr.write(f"[parse] [{i+1}/{len(sessions)}] {path.name} ...\n")
        try:
            events = parse_transcript(path)
        except Exception as e:
            sys.stderr.write(f"[parse] skip {path.name}: {e}\n")
            continue
        pos, neg = extract_pairs(events, window)
        all_positives.extend(pos)
        all_negatives.extend(neg)

    sys.stderr.write(
        f"[extract] {len(all_positives)} SSL positives, "
        f"{len(all_negatives)} candidate negatives\n"
    )

    # Deduplicate by user_text
    seen_inputs: set[str] = set()
    dedup_pos: list[tuple[str, str]] = []
    for user_text, mem in all_positives:
        key = user_text[:200]
        if key not in seen_inputs:
            seen_inputs.add(key)
            dedup_pos.append((user_text, mem))

    # Sample negatives at neg_ratio × positives
    neg_target = int(len(dedup_pos) * neg_ratio)
    random.shuffle(all_negatives)
    sampled_neg = []
    for t in all_negatives:
        key = t[:200]
        if key not in seen_inputs and len(t) > 15:
            seen_inputs.add(key)
            sampled_neg.append(t)
            if len(sampled_neg) >= neg_target:
                break

    sys.stderr.write(
        f"[sample] {len(dedup_pos)} positives, {len(sampled_neg)} negatives "
        f"(ratio {len(sampled_neg)/max(1,len(dedup_pos)):.1f}x)\n"
    )

    if dry_run:
        print(f"[dry-run] positives: {len(dedup_pos)}, negatives: {len(sampled_neg)}")
        print("\nSample positives:")
        for user_text, mem in dedup_pos[:5]:
            print(f"  INPUT: {user_text[:100]}")
            print(f"  LABEL: {mem[:100]}")
            print()
        print("Sample negatives (first 3):")
        for t in sampled_neg[:3]:
            print(f"  {t[:100]}")
        return

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    total_written = pos_written = neg_written = 0

    with open(out_path, "w") as fout:
        # Phase 2: convert SSL memories to natural-language hints
        sys.stderr.write(f"[convert] converting {len(dedup_pos)} SSL memories → hints...\n")
        mem_contents = [mem for _, mem in dedup_pos]
        user_texts_pos = [u for u, _ in dedup_pos]

        batches_pos = [mem_contents[i:i+batch_size] for i in range(0, len(mem_contents), batch_size)]
        hints_pos: list[str] = []
        for bi, batch in enumerate(batches_pos):
            sys.stderr.write(f"[convert] batch {bi+1}/{len(batches_pos)}...\n")
            hints_pos.extend(ssl_to_hint_batch(batch, ollama_url, ollama_model))
            time.sleep(0.3)

        for user_text, hint in zip(user_texts_pos, hints_pos):
            fout.write(json.dumps(make_example(user_text, hint)) + "\n")
            total_written += 1
            if hint:
                pos_written += 1

        # Phase 3: label negatives
        sys.stderr.write(f"[label] labeling {len(sampled_neg)} negatives...\n")
        batches_neg = [sampled_neg[i:i+batch_size] for i in range(0, len(sampled_neg), batch_size)]
        hints_neg: list[str] = []
        for bi, batch in enumerate(batches_neg):
            sys.stderr.write(f"[neg-label] batch {bi+1}/{len(batches_neg)}...\n")
            hints_neg.extend(label_negatives_batch(batch, ollama_url, ollama_model))
            time.sleep(0.3)

        for user_text, hint in zip(sampled_neg, hints_neg):
            fout.write(json.dumps(make_example(user_text, hint)) + "\n")
            total_written += 1
            if hint:
                pos_written += 1
            else:
                neg_written += 1

    sys.stderr.write(
        f"[done] {total_written} examples → {out_path}\n"
        f"       {pos_written} with hints / {neg_written + (total_written - pos_written - neg_written)} negatives\n"
    )
    print(json.dumps({
        "total": total_written,
        "pos": pos_written,
        "ssl_pairs": len(dedup_pos),
        "neg_sampled": len(sampled_neg),
        "out": out_path,
    }))


def main() -> None:
    parser = argparse.ArgumentParser(description="Build SSL hint corpus from transcripts")
    parser.add_argument("--projects-dir",  default=DEFAULT_PROJECTS_DIR)
    parser.add_argument("--out",           default=DEFAULT_OUT)
    parser.add_argument("--max-sessions",  type=int, default=300,
                        help="Max transcript files to scan (most recent first, default 300)")
    parser.add_argument("--neg-ratio",     type=float, default=2.0,
                        help="Negative examples per positive (default 2.0)")
    parser.add_argument("--window",        type=int, default=25,
                        help="Look-back window (events) to find user turn for a remember call (default 25)")
    parser.add_argument("--ollama-url",    default=OLLAMA_URL)
    parser.add_argument("--ollama-model",  default="qwen3.6:27b")
    parser.add_argument("--batch",         type=int, default=15)
    parser.add_argument("--dry-run",       action="store_true")
    args = parser.parse_args()

    run(
        projects_dir=args.projects_dir,
        out_path=args.out,
        max_sessions=args.max_sessions,
        neg_ratio=args.neg_ratio,
        window=args.window,
        ollama_url=args.ollama_url,
        ollama_model=args.ollama_model,
        batch_size=args.batch,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
