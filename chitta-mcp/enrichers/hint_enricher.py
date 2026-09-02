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

from __future__ import annotations

import argparse
import glob
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from http.client import HTTPException

CHITTA_BIN = os.environ.get("CHITTA_BIN", os.path.expanduser("~/.claude/bin/chitta"))
CHITTAD_BIN = os.environ.get("CHITTAD_BIN", os.path.expanduser("~/.claude/bin/chittad"))
DEFAULT_MIND = os.environ.get("MIND", os.path.expanduser("~/.claude/mind"))
# chitta-hint-tuned is gemma4:e4b with the system prompt baked in (fast, 4B MoE).
# Falls back to gemma4:26b if chitta-hint-tuned hasn't been created yet.
# NOTE: CHITTA_HINT_OLLAMA_MODEL is the Ollama model *name* for this enricher.
# Do NOT reuse CHITTA_HINT_MODEL here — that var is a GGUF *path* in the realtime
# hook / HintYantra, and overloading it mis-routes the backend.
DEFAULT_MODEL = os.environ.get("CHITTA_HINT_OLLAMA_MODEL", "chitta-hint-tuned")
if os.environ.get("CHITTA_HINT_MODEL") and not os.environ.get("CHITTA_HINT_OLLAMA_MODEL"):
    sys.stderr.write(
        "[hint_enricher] note: CHITTA_HINT_MODEL is now a GGUF path elsewhere; "
        "set CHITTA_HINT_OLLAMA_MODEL for this enricher's Ollama model name\n"
    )
FALLBACK_MODEL = "gemma4:26b"
DEFAULT_LIMIT = 100
LLM_TIMEOUT = 30  # chitta-hint is fast (~1-2s); 30s is generous

# When using chitta-hint-tuned (system prompt baked into Modelfile), send raw content.
# When using the fallback model, wrap with instructions.
PROMPT_CHITTA_HINT = "{content}"
PROMPT_FALLBACK = (
    "Rewrite as a short third-person retrieval fact (8-15 words, no explanation):\n"
    '"{content}"\n'
    "Fact:"
)
# Select template based on model name at call time (see generate_hint)
PROMPT_TEMPLATE = PROMPT_CHITTA_HINT  # default; overridden per-call for fallback model

# Kinds that are worth enriching — skip code/symbol/artifact memories
ENRICHABLE_KINDS = {
    "episode",
    "signal",
    "wisdom",
    "observation",
    "fact",
    "correction",
    "preference",
    "habit",
}

# Prefixes to strip before passing to the hint model (no filtering — model decides)
USER_PREFIXES = (
    "[user]",
    "[human]",
    "user:",
    "human:",
    "[compliance:auto]",
    "user correction:",
    "User correction:",
)


def discover_ollama() -> str:
    for path in glob.glob("/tmp/ollama-server-*.url"):
        try:
            url = open(path).read().strip()
            if not url:
                continue
            with urllib.request.urlopen(f"{url}/v1/models", timeout=3):
                return url
        except (OSError, HTTPException):
            # Probing candidate Ollama endpoints: unreadable pointer file, no
            # listener, or a timeout all mean "not this one, try the next".
            continue
    try:
        with urllib.request.urlopen("http://localhost:11434/v1/models", timeout=3):
            return "http://localhost:11434"
    except (OSError, HTTPException):
        # No local Ollama either; "" tells the caller to use another backend.
        pass
    return ""


def generate_hint(endpoint: str, model: str, content: str) -> str:
    """Call Ollama /api/generate and return the hint text, or '' on failure."""
    # Strip role prefix for cleaner input
    text = content.strip()
    for prefix in USER_PREFIXES:
        if text.lower().startswith(prefix):
            text = text[len(prefix) :].strip()
            break

    # Skip very short or whitespace-only content
    if len(text) < 10:
        return ""

    template = PROMPT_CHITTA_HINT if "chitta-hint" in model else PROMPT_FALLBACK
    prompt = template.format(content=text[:500])
    body = json.dumps(
        {
            "model": model,
            "prompt": prompt,
            "stream": False,
            "keep_alive": "30m",
            "options": {"temperature": 0.1, "num_predict": 64},
        }
    ).encode()
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
    except (OSError, HTTPException, ValueError, KeyError) as e:
        # Network failure, or a response that is not the chat-completions shape.
        # An empty hint is skipped by the caller; enrichment is optional.
        sys.stderr.write(f"[hint_enricher] Ollama call failed: {e}\n")
        return ""


def chitta(args: list, mind: str, input_text: str | None = None) -> str:
    env = os.environ.copy()
    env["MIND"] = mind
    try:
        result = subprocess.run(
            [CHITTA_BIN] + args,
            capture_output=True,
            text=True,
            timeout=30,
            input=input_text,
            env=env,
        )
        return result.stdout.strip()
    except (OSError, subprocess.SubprocessError) as e:
        # chitta binary missing or over its 30s timeout.
        sys.stderr.write(f"[hint_enricher] chitta {args[0]} failed: {e}\n")
        return ""


def list_memories(mind: str, limit: int) -> list[dict]:
    """Return memories that need hints: kind in ENRICHABLE_KINDS, no hint:done tag."""
    # Cap at 60 to stay under the ~200KB socket response threshold where the
    # daemon falls back to raw JSON array format (without tags field).
    fetch = min(limit * 3, 60)
    raw = chitta(["list_memories_brief", "--limit", str(fetch)], mind)
    if not raw:
        return []

    # Parse: daemon returns JSONL for small responses, JSON array for large ones.
    rows: list[dict] = []
    stripped = raw.strip()
    if stripped.startswith("["):
        try:
            rows = json.loads(stripped)
        except json.JSONDecodeError:
            pass
    else:
        for line in stripped.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue

    memories = []
    for m in rows:
        if not isinstance(m, dict):
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
        if not content or len(content.strip()) < 15:
            continue
        memories.append(
            {
                "id": m.get("id") or m.get("memory_id", ""),
                "realm": m.get("realm", "default"),
                "content": content,
            }
        )
        if len(memories) >= limit:
            break
    return memories


def store_hint(mind: str, realm: str, hint: str) -> bool:
    """Store the hint as a derived memory in the same realm."""
    out = chitta(
        [
            "remember",
            "--kind",
            "hint",
            "--realm",
            realm,
            "--tags",
            "retrieval_hint",
            hint,
        ],
        mind,
    )
    return bool(out)


def tag_done(mind: str, memory_id) -> None:
    """Tag the original memory so we don't re-process it."""
    chitta(["tag", "--id", str(memory_id), "--add", "hint:done"], mind)


def _check_inline(mind: str) -> bool:
    """Return True if chittad hint_extract is available with the hint model loaded."""
    try:
        r = subprocess.run(
            [CHITTAD_BIN, "--path", mind, "hint_extract", "test"],
            capture_output=True,
            text=True,
            timeout=15,
        )
        return r.returncode == 0
    except (OSError, subprocess.SubprocessError):
        # Capability probe: chittad absent or too slow means "not available".
        return False


def _check_llama_cpp() -> bool:
    """Return True if llama_cpp Python is importable and the GGUF model exists."""
    model_path = os.environ.get(
        "CHITTA_HINT_GGUF",
        os.path.expanduser("~/.claude/models/chitta-hint-qwen-q4_k_m.gguf"),
    )
    if not os.path.exists(model_path):
        return False
    try:
        import importlib.util

        return importlib.util.find_spec("llama_cpp") is not None
    except (ImportError, ValueError):
        # find_spec raises ValueError for a package in a broken import state.
        return False


_llama_cpp_instance = None


def hint_extract_llama_cpp_batch(memories: list[dict]) -> list[str]:
    """Run hint extraction via llama_cpp Python (no daemon, no Ollama)."""
    global _llama_cpp_instance
    SYSTEM = (
        "Extract a single concise retrieval hint from the message. "
        "Cover: personal preferences, tech choices, developer workflows, "
        "domain expertise, project facts, and recurring patterns. "
        "If nothing factual is present, output nothing."
    )
    model_path = os.environ.get(
        "CHITTA_HINT_GGUF",
        os.path.expanduser("~/.claude/models/chitta-hint-qwen-q4_k_m.gguf"),
    )
    try:
        from llama_cpp import Llama

        if _llama_cpp_instance is None:
            _llama_cpp_instance = Llama(
                model_path=model_path,
                n_ctx=512,
                n_gpu_layers=0,
                verbose=False,
            )
        llm = _llama_cpp_instance
    except Exception as e:  # noqa: BLE001
        # Loading a GGUF through llama_cpp reaches native code: a missing
        # package, an incompatible build, a corrupt model, and an OOM all
        # surface as different types. Every one means "no local backend", and
        # the caller falls back to the other hint paths.
        sys.stderr.write(f"[hint_enricher] llama_cpp load failed: {e}\n")
        return [""] * len(memories)

    results = []
    for m in memories:
        text = _strip_prefix(m["content"].strip())[:500]
        if len(text) < 10:
            results.append("")
            continue
        prompt = (
            f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
            f"<|im_start|>user\n{text}<|im_end|>\n"
            f"<|im_start|>assistant\n"
        )
        try:
            out = llm(prompt, max_tokens=80, temperature=0.0, stop=["<|im_end|>"])
            results.append(_clean_hint(out["choices"][0]["text"]))
        except Exception as e:  # noqa: BLE001
            # Same native-code surface as the load above. One memory failing to
            # generate a hint must not abandon the rest of the batch, so the
            # slot gets "" and the loop continues.
            sys.stderr.write(f"[hint_enricher] llama_cpp inference failed: {e}\n")
            results.append("")
    return results


def _strip_prefix(text: str) -> str:
    for prefix in USER_PREFIXES:
        if text.lower().startswith(prefix):
            return text[len(prefix) :].strip()
    return text


def _clean_hint(raw: str) -> str:
    hint = raw.strip()
    if not hint or hint.startswith("-") or len(hint) < 5:
        return ""
    for sep in (".", "\n"):
        idx = hint.find(sep)
        if 0 < idx < 120:
            hint = hint[: idx + 1].strip()
            break
    return hint[:150]


def hint_extract_batch(memories: list[dict], mind: str) -> list[str]:
    """Run chittad hint_extract in batch mode. One process load, N inferences."""
    texts = []
    for m in memories:
        t = _strip_prefix(m["content"].strip())
        texts.append(t[:500] if len(t) >= 10 else "")

    batch_input = "\n".join(t.replace("\n", " ") for t in texts) + "\n"
    try:
        r = subprocess.run(
            [CHITTAD_BIN, "--path", mind, "hint_extract"],
            input=batch_input,
            capture_output=True,
            text=True,
            timeout=300,
        )
        if r.returncode != 0:
            sys.stderr.write(
                f"[hint_enricher] chittad hint_extract rc={r.returncode}: {r.stderr[:200]}\n"
            )
            return [""] * len(memories)
        lines = r.stdout.splitlines()
        # Pad or truncate to match input count
        lines += [""] * max(0, len(memories) - len(lines))
        return [_clean_hint(line) for line in lines[: len(memories)]]
    except (OSError, subprocess.SubprocessError) as e:
        # chittad missing or over its timeout; the batch yields no hints.
        sys.stderr.write(f"[hint_enricher] hint_extract_batch failed: {e}\n")
        return [""] * len(memories)


def run(mind: str, limit: int, model: str, dry_run: bool) -> None:
    # Priority: chittad hint_extract → llama_cpp Python → Ollama HTTP
    use_inline = _check_inline(mind)
    use_llama_cpp = False
    endpoint = ""

    if not use_inline:
        use_llama_cpp = _check_llama_cpp()
    if not use_inline and not use_llama_cpp:
        endpoint = discover_ollama()
        if not endpoint:
            sys.stderr.write(
                "[hint_enricher] no inference backend available (chittad/llama_cpp/ollama)\n"
            )
            sys.exit(1)

    if use_inline:
        mode = "inline(chittad)"
    elif use_llama_cpp:
        mode = "llama_cpp(python)"
    else:
        mode = f"ollama({endpoint})"
    sys.stderr.write(f"[hint_enricher] mode={mode} model={model} limit={limit}\n")

    memories = list_memories(mind, limit)
    sys.stderr.write(f"[hint_enricher] {len(memories)} memories to enrich\n")

    if use_inline:
        hints = hint_extract_batch(memories, mind)
    elif use_llama_cpp:
        hints = hint_extract_llama_cpp_batch(memories)
    else:
        hints = [generate_hint(endpoint, model, m["content"]) for m in memories]

    done = 0
    skipped = 0
    for m, hint in zip(memories, hints):
        if not hint:
            skipped += 1
            sys.stderr.write(f"[hint_enricher] skip {str(m['id'])[:16]} (no hint)\n")
            if not dry_run:
                tag_done(mind, m["id"])
            continue

        sys.stderr.write(f"[hint_enricher] {str(m['id'])[:16]} → {hint!r}\n")
        if not dry_run:
            if store_hint(mind, m["realm"], hint):
                tag_done(mind, m["id"])
                done += 1
            else:
                sys.stderr.write(f"[hint_enricher] store failed for {str(m['id'])[:16]}\n")
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
