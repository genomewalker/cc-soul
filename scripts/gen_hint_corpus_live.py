#!/usr/bin/env python3
"""
gen_hint_corpus_live.py — Generate diverse hint extraction training corpus.

Sources (combined):
  1. HuggingFace public datasets (PersonaChat, MSC) — existing conv→fact pairs
  2. Soul memory samples (chitta-field) — labeled by local model
  3. Synthetic domain templates from generate_hint_corpus.py helpers (optional)

Labeler backends (in priority order):
  1. claude -p  (Claude Code CLI, no API key needed)
  2. Ollama     (local, requires running server)

Output: ShareGPT-format JSONL for fine-tuning chitta-hint-qwen.

Usage:
    python3 gen_hint_corpus_live.py \\
        [--limit 1200] [--out PATH] [--mind PATH] [--batch 20] \\
        [--hf-limit 400] [--soul-limit 600] \\
        [--ollama-model qwen3.6:27b] [--dry-run]
"""

import argparse
import json
import os
import random
import re
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

CHITTA_BIN   = os.environ.get("CHITTA_BIN",  os.path.expanduser("~/.claude/bin/chitta"))
DEFAULT_OUT  = "/maps/projects/caeg/scratch/kbd606/tmp/hint_corpus_chatml.jsonl"
DEFAULT_MIND = os.environ.get("MIND", os.path.expanduser("~/.claude/mind"))
OLLAMA_URL   = "http://dandygpun01fl:11434"
DEFAULT_OLLAMA_MODEL = "qwen3.6:27b"

SYSTEM_PROMPT = (
    "Extract a single concise retrieval hint from the message. "
    "Cover: personal preferences, tech choices (languages, editors, tools, configs), "
    "developer workflows, domain expertise, project facts, and recurring patterns. "
    "If nothing factual is present, output nothing."
)

SAMPLE_KINDS = {
    "episode", "signal", "wisdom", "observation", "fact",
    "correction", "preference", "habit", "belief", "goal",
    "milestone",
}
KIND_CAP     = 150   # max per kind before dedup sampling
FETCH_LIMIT  = 6000

# ---------------------------------------------------------------------------
# Labeler prompt
# ---------------------------------------------------------------------------

LABELER_INSTRUCTIONS = """You are labeling training examples for a hint-extraction model.

Given a list of numbered memories from an AI assistant's memory store, output a JSON
array of hint strings — one per input, in order. Each element is either:
  - A concise retrieval hint (8–25 words, third person, present tense)
  - An empty string "" if nothing factual/useful is extractable

Coverage rules — extract from ALL of these:
- SHORTHAND "[arch] event-sourcing>audit-trail|auditable"
    → "User prefers event-sourcing for audit trails in distributed systems."
- BIOINFORMATICS "[bioinformatics] Evo2-role>contamination-axis|orthogonal-to-completeness"
    → "User studies Evo2 as a contamination detection signal orthogonal to genome completeness."
- ARROW/PIPE NOTATION "A $\\to$ B | C" means "A leads to B, alternatively C" — decode it.
- TECH PREFERENCES "I use neovim with lazy.nvim"
    → "User uses Neovim with lazy.nvim as their primary editor."
- DEVELOPER WORKFLOW "always run tests before committing"
    → "User runs tests before every commit."
- PROJECT FACTS "working on fastst for microbial source tracking"
    → "User is developing fastst, a microbial source-tracking tool."
- TEMPORAL FACTS "paper deadline is June 15"
    → "User has a paper deadline on June 15."
- CONFIG/TOOL CHOICES: extract as preferences.
- PURE NOISE, errors, role-play fragments, empty ping → "".
- TURN TRANSCRIPTS (start "turn_user"/"turn_assistant") → extract only if a clear
  preference, decision, or factual claim appears; otherwise "".

Output ONLY the JSON array. No explanation, no markdown fences, no other text."""


# ---------------------------------------------------------------------------
# Labeler backends
# ---------------------------------------------------------------------------

def _try_codex_exec(batch_text: str, model: str = "gpt-5.5") -> str | None:
    """Call `codex exec` non-interactively. Returns last-message text or None."""
    import tempfile
    prompt = LABELER_INSTRUCTIONS + "\n\n" + batch_text
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        out_path = tf.name
    try:
        result = subprocess.run(
            ["codex", "exec", "-m", model, "--json", "-o", out_path],
            input=prompt, capture_output=True, text=True, timeout=180,
        )
        if not os.path.exists(out_path):
            return None
        # Last non-empty line of JSONL is the final response text
        lines = [l.strip() for l in open(out_path).readlines() if l.strip()]
        if lines:
            # codex exec appends the final text as the last plain line
            return lines[-1]
    except Exception as e:
        sys.stderr.write(f"[labeler] codex exec failed: {e}\n")
    finally:
        try:
            os.unlink(out_path)
        except OSError:
            pass
    return None


def _try_gemma(batch_text: str, model: str = "gemma3:27b") -> str | None:
    """Call Ollama with a Gemma model as second-tier labeler."""
    return _try_ollama(batch_text, model)


def _try_ollama(batch_text: str, model: str) -> str | None:
    """Call Ollama /api/generate. Returns raw text or None on failure."""
    import urllib.request
    prompt = LABELER_INSTRUCTIONS + "\n\n" + batch_text
    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": 0.1, "num_predict": 2048},
    }).encode()
    req = urllib.request.Request(
        f"{OLLAMA_URL}/api/generate",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            resp = json.loads(r.read())
        text = resp.get("response", "").strip()
        # Strip Qwen3 thinking block if present
        if "<think>" in text:
            text = re.sub(r"<think>.*?</think>", "", text, flags=re.DOTALL).strip()
        return text if text else None
    except Exception as e:
        sys.stderr.write(f"[labeler] ollama failed: {e}\n")
    return None


def _parse_json_array(text: str, expected: int) -> list[str]:
    """Parse a JSON array from model output, with fallback extraction."""
    text = re.sub(r"^```[^\n]*\n?", "", text)
    text = re.sub(r"\n?```$", "", text.rstrip())
    try:
        arr = json.loads(text)
        if isinstance(arr, list) and len(arr) == expected:
            return [str(h).strip() for h in arr]
        if isinstance(arr, list):
            # Pad or truncate
            arr = arr[:expected] + [""] * max(0, expected - len(arr))
            return [str(h).strip() for h in arr]
    except json.JSONDecodeError:
        pass
    # Fallback: try to extract bracketed block
    m = re.search(r"\[.*\]", text, re.DOTALL)
    if m:
        try:
            arr = json.loads(m.group(0))
            if isinstance(arr, list):
                arr = arr[:expected] + [""] * max(0, expected - len(arr))
                return [str(h).strip() for h in arr]
        except json.JSONDecodeError:
            pass
    sys.stderr.write(f"[labeler] could not parse array from output (len={len(text)})\n")
    return [""] * expected


def label_batch(memories: list[dict], ollama_model: str,
                codex_model: str = "gpt-5.5",
                gemma_model: str = "gemma3:27b") -> list[str]:
    """Label a batch: codex exec (gpt-5.5) → gemma (ollama) → fallback ollama model."""
    numbered = "\n\n".join(
        f"[{i+1}] {m['content'][:500]}" for i, m in enumerate(memories)
    )
    batch_text = f"Label these {len(memories)} memories:\n\n{numbered}"

    text = _try_codex_exec(batch_text, codex_model)
    if text is None:
        sys.stderr.write(f"[labeler] codex failed, trying gemma ({gemma_model})\n")
        text = _try_gemma(batch_text, gemma_model)
    if text is None:
        sys.stderr.write(f"[labeler] gemma failed, trying ollama ({ollama_model})\n")
        text = _try_ollama(batch_text, ollama_model)
    if text is None:
        sys.stderr.write("[labeler] all backends failed — returning empty batch\n")
        return [""] * len(memories)
    return _parse_json_array(text, len(memories))


# ---------------------------------------------------------------------------
# Source 1: HuggingFace public datasets
# ---------------------------------------------------------------------------

def _hf_personachat(limit: int) -> list[dict]:
    """AlekseyKorshuk/persona-chat: persona sentences as first-person facts."""
    examples = []
    try:
        from datasets import load_dataset
        ds = load_dataset("AlekseyKorshuk/persona-chat", split="train", streaming=True)
        seen: set[str] = set()
        for row in ds:
            for p in row.get("personality", []):
                p = p.strip()
                if not p or p in seen or len(p) < 10:
                    continue
                seen.add(p)
                examples.append({"content": p, "_source": "personachat"})
                if len(examples) >= limit:
                    return examples
    except Exception as e:
        sys.stderr.write(f"[hf] persona-chat failed: {e}\n")
    return examples


def _hf_dolly(limit: int) -> list[dict]:
    """databricks/databricks-dolly-15k: diverse real instructions across many domains."""
    examples = []
    try:
        from datasets import load_dataset
        ds = load_dataset("databricks/databricks-dolly-15k", split="train", streaming=True)
        seen: set[str] = set()
        for row in ds:
            text = (row.get("instruction") or "").strip()
            ctx = (row.get("context") or "").strip()
            # prefer instructions with context (richer domain content)
            combined = f"{text}\n{ctx}".strip() if ctx else text
            if not combined or combined in seen or len(combined) < 20 or len(combined) > 800:
                continue
            seen.add(combined)
            examples.append({"content": combined, "_source": "dolly"})
            if len(examples) >= limit:
                return examples
    except Exception as e:
        sys.stderr.write(f"[hf] dolly failed: {e}\n")
    return examples


def load_hf_sources(limit: int) -> list[dict]:
    """Load and combine HuggingFace sources."""
    per_source = limit // 2
    rows: list[dict] = []
    rows.extend(_hf_personachat(per_source))
    # Fill remainder with diverse prompts if personachat short
    remaining = limit - len(rows)
    if remaining > 0:
        rows.extend(_hf_dolly(remaining))
    random.shuffle(rows)
    sys.stderr.write(f"[hf] {len(rows)} examples from public datasets\n")
    return rows[:limit]


# ---------------------------------------------------------------------------
# Source 2: Soul memory samples
# ---------------------------------------------------------------------------

def fetch_soul_memories(mind: str) -> list[dict]:
    env = os.environ.copy()
    env["MIND"] = mind
    result = subprocess.run(
        [CHITTA_BIN, "list_memories_brief", "--limit", str(FETCH_LIMIT)],
        capture_output=True, text=True, timeout=300, env=env,
    )
    rows = []
    raw = result.stdout.strip()
    if raw.startswith("["):
        try:
            rows = json.loads(raw)
        except json.JSONDecodeError:
            pass
    else:
        for line in raw.splitlines():
            line = line.strip()
            if line:
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    return rows


def stratified_sample_soul(rows: list[dict], target: int) -> list[dict]:
    by_kind: dict[str, list[dict]] = {}
    for r in rows:
        kind = r.get("kind", "")
        if kind not in SAMPLE_KINDS:
            continue
        content = r.get("content", "").strip()
        if len(content) < 15:
            continue
        if "hint:done" in r.get("tags", []):
            continue
        by_kind.setdefault(kind, []).append(r)

    pool = []
    for bucket in by_kind.values():
        random.shuffle(bucket)
        pool.extend(bucket[:KIND_CAP])

    random.shuffle(pool)
    sys.stderr.write(
        f"[soul] {len(pool)} candidates across kinds: "
        + ", ".join(f"{k}:{min(len(v), KIND_CAP)}" for k, v in sorted(by_kind.items()))
        + "\n"
    )
    return random.sample(pool, min(len(pool), target))


# ---------------------------------------------------------------------------
# Corpus writing
# ---------------------------------------------------------------------------

def make_example(content: str, hint: str) -> dict:
    return {
        "conversations": [
            {"from": "system", "value": SYSTEM_PROMPT},
            {"from": "human",  "value": content},
            {"from": "gpt",    "value": hint},
        ]
    }


def process_hf_rows(rows: list[dict], batch_size: int,
                    ollama_model: str, codex_model: str, fout,
                    gemma_model: str = "gemma3:27b") -> tuple[int, int]:
    pos = neg = 0
    batches = [rows[i:i+batch_size] for i in range(0, len(rows), batch_size)]
    for bi, batch in enumerate(batches):
        sys.stderr.write(f"[hf-label] batch {bi+1}/{len(batches)}...\n")
        hints = label_batch(batch, ollama_model, codex_model, gemma_model)
        for mem, hint in zip(batch, hints):
            fout.write(json.dumps(make_example(mem["content"], hint)) + "\n")
            if hint: pos += 1
            else:    neg += 1
        if bi < len(batches) - 1:
            time.sleep(0.5)
    return pos, neg


def process_soul_rows(rows: list[dict], batch_size: int,
                      ollama_model: str, codex_model: str, fout,
                      gemma_model: str = "gemma3:27b") -> tuple[int, int]:
    pos = neg = 0
    batches = [rows[i:i+batch_size] for i in range(0, len(rows), batch_size)]
    for bi, batch in enumerate(batches):
        sys.stderr.write(f"[soul-label] batch {bi+1}/{len(batches)}...\n")
        hints = label_batch(batch, ollama_model, codex_model, gemma_model)
        for mem, hint in zip(batch, hints):
            content = mem.get("content", "").strip()
            fout.write(json.dumps(make_example(content, hint)) + "\n")
            if hint: pos += 1
            else:    neg += 1
        if bi < len(batches) - 1:
            time.sleep(0.5)
    return pos, neg


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run(mind: str, hf_limit: int, soul_limit: int, out_path: str,
        batch_size: int, ollama_model: str, codex_model: str,
        gemma_model: str, dry_run: bool) -> None:

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    # --- HuggingFace source ---
    hf_rows = load_hf_sources(hf_limit)

    # --- Soul source ---
    sys.stderr.write(f"[gen] fetching soul memories from {mind}\n")
    soul_raw = fetch_soul_memories(mind)
    soul_rows = stratified_sample_soul(soul_raw, soul_limit)
    sys.stderr.write(f"[gen] soul sample: {len(soul_rows)} memories\n")

    total = len(hf_rows) + len(soul_rows)
    sys.stderr.write(f"[gen] total to label: {total}\n")

    if dry_run:
        sys.stderr.write("[gen] --dry-run: showing 3 from each source\n")
        for r in hf_rows[:3]:
            print(json.dumps({"source": r["_source"], "content": r["content"][:100]}))
        for r in soul_rows[:3]:
            print(json.dumps({"source": "soul", "kind": r.get("kind"),
                               "content": r.get("content","")[:100]}))
        return

    total_pos = total_neg = 0
    with open(out_path, "w") as fout:
        p, n = process_hf_rows(hf_rows, batch_size, ollama_model, codex_model, fout,
                               gemma_model=gemma_model)
        total_pos += p; total_neg += n
        sys.stderr.write(f"[gen] HF done: pos={p} neg={n}\n")

        p, n = process_soul_rows(soul_rows, batch_size, ollama_model, codex_model, fout,
                                 gemma_model=gemma_model)
        total_pos += p; total_neg += n
        sys.stderr.write(f"[gen] soul done: pos={p} neg={n}\n")

    total_ex = total_pos + total_neg
    sys.stderr.write(
        f"[gen] corpus: {total_ex} examples "
        f"({total_pos} with hints / {total_neg} negatives) → {out_path}\n"
    )
    print(json.dumps({
        "total": total_ex, "pos": total_pos, "neg": total_neg,
        "out": out_path,
        "hf": len(hf_rows), "soul": len(soul_rows),
    }))


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate hint training corpus")
    parser.add_argument("--hf-limit",    type=int, default=400,
                        help="Max examples from HuggingFace datasets (default 400)")
    parser.add_argument("--soul-limit",  type=int, default=800,
                        help="Max examples from soul memories (default 800)")
    parser.add_argument("--out",         default=DEFAULT_OUT)
    parser.add_argument("--mind",        default=DEFAULT_MIND)
    parser.add_argument("--batch",       type=int, default=20)
    parser.add_argument("--ollama-model", default=DEFAULT_OLLAMA_MODEL,
                        help=f"Ollama fallback model (default {DEFAULT_OLLAMA_MODEL})")
    parser.add_argument("--codex-model",  default="gpt-5.5",
                        help="codex exec model (default gpt-5.5)")
    parser.add_argument("--gemma-model",  default="gemma3:27b",
                        help="Gemma/Ollama second-tier model (default gemma3:27b)")
    parser.add_argument("--dry-run",     action="store_true")
    args = parser.parse_args()

    run(
        mind=args.mind,
        hf_limit=args.hf_limit,
        soul_limit=args.soul_limit,
        out_path=args.out,
        batch_size=args.batch,
        ollama_model=args.ollama_model,
        codex_model=args.codex_model,
        gemma_model=args.gemma_model,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
