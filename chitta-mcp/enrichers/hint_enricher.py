#!/usr/bin/env python3
"""
Retrieval-hint enricher — generates short semantic retrieval facts for memories
using a local Ollama model (default: gemma4:26b) and stores them as derived
memories so they are automatically included in BM25 and semantic recall.

Usage:
    python3 hint_enricher.py [--mind PATH] [--limit N] [--model MODEL] [--dry-run]

Reads unprocessed memories (those without a `retrieval_hint` tag), calls Ollama
to generate a concise third-person fact, then stores the hint as a new memory
in the same realm with kind=hint and tags=retrieval_hint.

Marks processed memories with tag `hint:done` to avoid re-processing.
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error

CHITTA_BIN = os.environ.get("CHITTA_BIN", os.path.expanduser("~/.claude/bin/chitta"))
DEFAULT_MIND = os.environ.get("MIND", os.path.expanduser("~/.claude/mind"))
# chitta-hint-tuned is gemma4:e4b with the system prompt baked in (fast, 4B MoE).
# Falls back to gemma4:26b if chitta-hint-tuned hasn't been created yet.
DEFAULT_MODEL = os.environ.get("CHITTA_HINT_MODEL", "chitta-hint-tuned")
FALLBACK_MODEL = "gemma4:26b"
DEFAULT_LIMIT = 100
LLM_TIMEOUT = 30  # chitta-hint is fast (~1-2s); 30s is generous

# When using chitta-hint (baked system prompt), just send the raw content.
# When using the fallback model, wrap with instructions.
PROMPT_CHITTA_HINT = '"{content}"'
PROMPT_FALLBACK = (
    'Rewrite as a short third-person retrieval fact (8-15 words, no explanation):\n'
    '"{content}"\n'
    'Fact:'
)

# Kinds that are worth enriching — skip code/symbol/artifact memories
ENRICHABLE_KINDS = {"episode", "signal", "wisdom", "observation", "fact"}

# Content prefixes that indicate user turns worth enriching
USER_PREFIXES = ("[user]", "[human]", "user:", "human:")


def discover_ollama() -> str:
    for path in glob.glob("/tmp/ollama-server-*.url"):
        try:
            url = open(path).read().strip()
            if not url:
                continue
            with urllib.request.urlopen(f"{url}/v1/models", timeout=3):
                return url
        except Exception:
            continue
    try:
        with urllib.request.urlopen("http://localhost:11434/v1/models", timeout=3):
            return "http://localhost:11434"
    except Exception:
        pass
    return ""


def generate_hint(endpoint: str, model: str, content: str) -> str:
    """Call Ollama /api/generate and return the hint text, or '' on failure."""
    # Strip role prefix for cleaner input
    text = content.strip()
    for prefix in USER_PREFIXES:
        if text.lower().startswith(prefix):
            text = text[len(prefix):].strip()
            break

    # Skip very short or whitespace-only content
    if len(text) < 10:
        return ""

    prompt = PROMPT_TEMPLATE.format(content=text[:500])
    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": 0.1, "num_predict": 64},
    }).encode()
    req = urllib.request.Request(
        f"{endpoint}/api/generate",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=LLM_TIMEOUT) as r:
            resp = json.loads(r.read())
        hint = resp.get("response", "").strip()
        # Reject if model refused or returned placeholder
        if not hint or hint.startswith("-") or len(hint) < 5:
            return ""
        # Trim to first sentence
        for sep in (".", "\n"):
            idx = hint.find(sep)
            if 0 < idx < 120:
                hint = hint[: idx + 1].strip()
                break
        return hint[:150]
    except Exception as e:
        sys.stderr.write(f"[hint_enricher] Ollama call failed: {e}\n")
        return ""


def chitta(args: list, mind: str, input_text: str | None = None) -> str:
    env = os.environ.copy()
    env["MIND"] = mind
    try:
        result = subprocess.run(
            [CHITTA_BIN] + args,
            capture_output=True, text=True, timeout=30,
            input=input_text, env=env,
        )
        return result.stdout.strip()
    except Exception as e:
        sys.stderr.write(f"[hint_enricher] chitta {args[0]} failed: {e}\n")
        return ""


def list_memories(mind: str, limit: int) -> list[dict]:
    """Return memories that need hints: kind in ENRICHABLE_KINDS, no hint:done tag."""
    raw = chitta(["list_memories_brief", "--limit", str(limit * 3)], mind)
    if not raw:
        return []
    memories = []
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            m = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = m.get("kind", "")
        if kind not in ENRICHABLE_KINDS:
            continue
        tags = m.get("tags", [])
        if isinstance(tags, str):
            tags = [t.strip() for t in tags.split(",")]
        if "hint:done" in tags or "retrieval_hint" in tags:
            continue
        content = m.get("content", "") or m.get("text", "")
        if not content:
            continue
        # Only enrich user-turn content
        has_user_prefix = any(content.lower().startswith(p) for p in USER_PREFIXES)
        if not has_user_prefix:
            continue
        memories.append({
            "id": m.get("id") or m.get("memory_id", ""),
            "realm": m.get("realm", "default"),
            "content": content,
        })
        if len(memories) >= limit:
            break
    return memories


def store_hint(mind: str, realm: str, hint: str) -> bool:
    """Store the hint as a derived memory in the same realm."""
    out = chitta([
        "remember",
        "--kind", "hint",
        "--realm", realm,
        "--tags", "retrieval_hint",
        hint,
    ], mind)
    return bool(out)


def tag_done(mind: str, memory_id: str) -> None:
    """Tag the original memory so we don't re-process it."""
    chitta(["tag", memory_id, "hint:done"], mind)


def run(mind: str, limit: int, model: str, dry_run: bool) -> None:
    endpoint = discover_ollama()
    if not endpoint:
        sys.stderr.write("[hint_enricher] no Ollama endpoint found\n")
        sys.exit(1)
    sys.stderr.write(f"[hint_enricher] endpoint={endpoint} model={model} limit={limit}\n")

    memories = list_memories(mind, limit)
    sys.stderr.write(f"[hint_enricher] {len(memories)} memories to enrich\n")

    done = 0
    skipped = 0
    for m in memories:
        hint = generate_hint(endpoint, model, m["content"])
        if not hint:
            skipped += 1
            sys.stderr.write(f"[hint_enricher] skip {m['id'][:8]} (no hint)\n")
            if not dry_run:
                tag_done(mind, m["id"])
            continue

        sys.stderr.write(f"[hint_enricher] {m['id'][:8]} → {hint!r}\n")
        if not dry_run:
            if store_hint(mind, m["realm"], hint):
                tag_done(mind, m["id"])
                done += 1
            else:
                sys.stderr.write(f"[hint_enricher] store failed for {m['id'][:8]}\n")
        else:
            done += 1

    print(json.dumps({"enriched": done, "skipped": skipped, "total": len(memories)}))


def main() -> None:
    parser = argparse.ArgumentParser(description="Retrieval-hint enricher")
    parser.add_argument("--mind", default=DEFAULT_MIND)
    parser.add_argument("--limit", type=int, default=DEFAULT_LIMIT)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    run(args.mind, args.limit, args.model, args.dry_run)


if __name__ == "__main__":
    main()
