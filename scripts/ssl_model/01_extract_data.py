#!/usr/bin/env python3
"""
Extract (conversation_chunk, ssl_output) training pairs from:
  - chitta soul memories (already-distilled SSL wisdoms)
  - their source transcripts (matched via episode markers)

Output: JSONL file with {"input": "...", "output": "..."} pairs.

Usage:
    python 01_extract_data.py --output training_data.jsonl [--min-wisdoms 1] [--max-tokens 3000]
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path


# ── Config ────────────────────────────────────────────────────────────────────

CLAUDE_PROJECTS = Path.home() / ".claude" / "projects"
CHITTA_BIN = Path.home() / ".claude" / "bin" / "chitta"

MAX_CONV_TOKENS = 3000   # ~2500 words, fits a 4096-token model
MAX_OUTPUT_TOKENS = 600  # SSL output is compact

SYSTEM_PROMPT = (
    "You are a memory distiller. Given a conversation, extract the key learnings "
    "in SSL v0.3 format: [TYPE] [domain] subject→action→result @location A:v,a\n"
    "Types: SOLUTION, GOTCHA, DECISION, PATTERN, PREFERENCE, FAILURE\n"
    "Annotations: A:valence,arousal (required), F:FLAG (when significant), →@ref (cross-refs)\n"
    "Be concise. Use SSL arrows (→) and location refs (@file:line).\n"
    "Output ONLY SSL lines, one per line. No preamble, no explanation."
)

SSL_PATTERN = re.compile(
    r'^\[(SOLUTION|GOTCHA|DECISION|PATTERN|PREFERENCE|FAILURE)\]'
)


# ── Transcript parsing ────────────────────────────────────────────────────────

def parse_transcript(path: Path, skip_lines: int = 0) -> list[dict]:
    """Parse a Claude JSONL transcript into (role, text) turns."""
    turns = []
    try:
        with open(path) as f:
            for i, line in enumerate(f):
                if i < skip_lines:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue

                role = obj.get("type", obj.get("role", ""))
                if role not in ("user", "assistant"):
                    continue

                # Extract text content — content is inside obj["message"]["content"]
                content = obj.get("message", {}).get("content", [])
                if not content:
                    # Fallback: content directly on obj
                    content = obj.get("content", [])
                if isinstance(content, str):
                    text = content
                elif isinstance(content, list):
                    parts = []
                    for block in content:
                        if isinstance(block, dict) and block.get("type") == "text":
                            parts.append(block.get("text", ""))
                    text = "\n".join(parts)
                else:
                    continue

                text = text.strip()
                if text:
                    turns.append({"role": role, "text": text})
    except Exception as e:
        print(f"  [warn] parse_transcript {path}: {e}", file=sys.stderr)
    return turns


def build_conversation(turns: list[dict], max_tokens: int = MAX_CONV_TOKENS) -> str:
    """Build conversation string with smart truncation (keep recent turns)."""
    parts = []
    for t in turns:
        role = "USER" if t["role"] == "user" else "ASSISTANT"
        # Truncate long assistant turns (tool outputs, code)
        text = t["text"]
        if t["role"] == "assistant" and len(text) > 500:
            text = text[:500] + "... [truncated]"
        parts.append(f"{role}: {text}")

    full = "\n\n".join(parts)

    # Rough token estimate: 4 chars/token
    if len(full) // 4 <= max_tokens:
        return full

    # Keep most recent turns that fit
    kept = []
    budget = max_tokens * 4
    for part in reversed(parts):
        if len(part) > budget:
            break
        kept.insert(0, part)
        budget -= len(part)

    return "\n\n".join(kept)


# ── Chitta memory extraction ──────────────────────────────────────────────────

def get_wisdoms_by_session(min_wisdoms: int = 1) -> dict[str, list[str]]:
    """
    Load wisdom memories via chitta recall CLI.
    Returns {"__all__": [ssl_line, ...]}
    """
    import subprocess

    session_wisdoms: dict[str, list[str]] = {}

    try:
        result = subprocess.run(
            [str(CHITTA_BIN), "recall",
             "--query", "solution gotcha decision pattern preference failure",
             "--limit", "2000"],
            capture_output=True, text=True, errors="replace", timeout=60,
        )
        lines = []
        for line in result.stdout.splitlines():
            line = line.strip()
            # Keep lines that look like SSL or wisdom content
            if line and not line.startswith("Found ") and not line.startswith("[episode]"):
                lines.append(line)
        session_wisdoms["__all__"] = lines
        print(f"  {len(lines)} memories loaded via chitta recall")
    except Exception as e:
        print(f"  [warn] chitta recall failed: {e}", file=sys.stderr)

    return session_wisdoms


def find_transcript(session_id: str) -> Path | None:
    """Find the JSONL transcript for a session_id."""
    for project_dir in CLAUDE_PROJECTS.iterdir():
        if not project_dir.is_dir():
            continue
        candidate = project_dir / f"{session_id}.jsonl"
        if candidate.exists():
            return candidate
    return None


# ── Training pair generation ──────────────────────────────────────────────────

def generate_pairs_from_transcripts(
    output_path: Path,
    min_wisdoms: int,
    max_tokens: int,
    use_llm: bool,
) -> int:
    """
    Strategy 1: Use already-distilled sessions.
    Find transcripts that have been distilled, pair them with their SSL outputs.
    """
    pairs = []

    # Find all large transcripts
    transcripts = sorted(
        [p for p in CLAUDE_PROJECTS.rglob("*.jsonl")
         if "subagents" not in str(p) and p.stat().st_size > 20_000],
        key=lambda p: p.stat().st_size,
        reverse=True,
    )

    print(f"Found {len(transcripts)} candidate transcripts")

    # Get existing SSL wisdoms from chitta as a pool
    print("Loading SSL memories from chitta...")
    session_wisdoms = get_wisdoms_by_session(min_wisdoms)
    ssl_pool = session_wisdoms.get("__all__", [])
    print(f"  {len(ssl_pool)} SSL wisdom lines in pool")

    # For each transcript: parse, build conversation, use existing SSL as output
    # We'll chunk long transcripts into windows of ~50 turns
    CHUNK_SIZE = 40   # turns per training example
    CHUNK_OVERLAP = 10

    processed = 0
    for transcript_path in transcripts[:500]:  # cap at 500 for initial run
        session_id = transcript_path.stem
        turns = parse_transcript(transcript_path)
        if len(turns) < 5:
            continue

        # Slide a window over the turns
        for start in range(0, len(turns), CHUNK_SIZE - CHUNK_OVERLAP):
            chunk = turns[start:start + CHUNK_SIZE]
            if len(chunk) < 3:
                break

            conversation = build_conversation(chunk, max_tokens)
            if not conversation.strip():
                continue

            # For each chunk we need SSL output.
            # If we have session-specific wisdoms, use them.
            # Otherwise sample from the global pool based on content similarity.
            ssl_for_session = session_wisdoms.get(session_id, [])
            if not ssl_for_session and ssl_pool:
                # Use a random subset of the pool (at least gives the model
                # the SSL format — content alignment is imperfect but better
                # than nothing for format learning)
                import random
                ssl_for_session = random.sample(ssl_pool, min(5, len(ssl_pool)))

            if not ssl_for_session:
                continue

            ssl_output = "\n".join(ssl_for_session[:8])  # cap output length

            pairs.append({
                "input": conversation,
                "output": ssl_output,
                "session_id": session_id,
                "chunk_start": start,
            })

        processed += 1
        if processed % 50 == 0:
            print(f"  Processed {processed} transcripts, {len(pairs)} pairs so far")

    print(f"\nTotal pairs: {len(pairs)}")

    with open(output_path, "w") as f:
        for pair in pairs:
            f.write(json.dumps(pair) + "\n")

    return len(pairs)


def generate_pairs_with_distillation(
    output_path: Path,
    min_wisdoms: int,
    max_tokens: int,
) -> int:
    """
    Strategy 2: Run existing distillation on transcripts to generate labels.
    Uses HTTP to Ollama/vLLM (auto-discovered GPU endpoint). Higher quality but slower.
    """
    import subprocess, urllib.request, glob as globmod

    # Discover endpoint
    endpoint = None
    for path in globmod.glob("/tmp/ollama-server-*.url"):
        try:
            url = open(path).read().strip()
            urllib.request.urlopen(url + "/v1/models", timeout=3)
            endpoint = url
            break
        except Exception:
            pass
    if not endpoint:
        try:
            urllib.request.urlopen("http://localhost:11434/v1/models", timeout=3)
            endpoint = "http://localhost:11434"
        except Exception:
            print("No GPU endpoint found — skipping distillation labels")
            return 0

    transcripts = sorted(
        [p for p in CLAUDE_PROJECTS.rglob("*.jsonl")
         if "subagents" not in str(p) and p.stat().st_size > 50_000],
        key=lambda p: p.stat().st_size,
        reverse=True,
    )[:200]

    print(f"Generating labels for {len(transcripts)} transcripts via distillation ({endpoint})...")

    pairs = []
    for i, transcript_path in enumerate(transcripts):
        session_id = transcript_path.stem
        turns = parse_transcript(transcript_path)
        if len(turns) < 5:
            continue

        conversation = build_conversation(turns, max_tokens)

        from ssl_prompt import EXTRACTION_PROMPT
        prompt = EXTRACTION_PROMPT + "\n\n## Conversation\n\n" + conversation

        # Call LLM via HTTP
        try:
            import json as json_mod
            req_body = json_mod.dumps({
                "model": "gemma4:26b",
                "messages": [{"role": "user", "content": prompt}],
                "temperature": 0.3, "max_tokens": 4096,
            }).encode()
            req = urllib.request.Request(
                endpoint + "/v1/chat/completions",
                data=req_body,
                headers={"Content-Type": "application/json"},
            )
            resp = urllib.request.urlopen(req, timeout=180)
            data = json_mod.loads(resp.read().decode())
            ssl_output = data["choices"][0]["message"]["content"].strip()
            if not ssl_output:
                continue
        except Exception as e:
            print(f"  [warn] distillation failed for {session_id}: {e}")
            continue

        pairs.append({
            "input": conversation,
            "output": ssl_output,
            "session_id": session_id,
        })

        if (i + 1) % 10 == 0:
            print(f"  {i+1}/{len(transcripts)} done, {len(pairs)} pairs")

    with open(output_path, "w") as f:
        for pair in pairs:
            f.write(json.dumps(pair) + "\n")

    return len(pairs)


# ── Contrastive (DPO) corpus ──────────────────────────────────────────────────

def generate_contrastive_pairs(
    output_path: Path,
    n_sessions: int,
    max_tokens: int,
) -> int:
    """
    Strategy: contrastive / DPO pairs.
      chosen:   claude-haiku distillation of the actual conversation chunk
      rejected: random soul memories from a different session (wrong content)
    Output format: {"prompt": ..., "chosen": ..., "rejected": ...}
    """
    import random

    import subprocess

    # Verify claude -p is available
    try:
        r = subprocess.run(["claude", "-p", "hi", "--model", "claude-haiku-4-5-20251001"],
                           capture_output=True, text=True, timeout=30)
        if r.returncode != 0:
            raise RuntimeError(r.stderr.strip())
        print("  claude -p: OK")
    except Exception as e:
        print(f"  [error] claude -p not available: {e}")
        print("  Make sure claude CLI is on PATH and you are logged in.")
        return 0

    # Load rejection pool (existing soul memories — wrong content for any given conv)
    print("Loading rejection pool from chitta...")
    session_wisdoms = get_wisdoms_by_session()
    rejection_pool = session_wisdoms.get("__all__", [])
    if not rejection_pool:
        print("  [warn] No rejection pool — rejected examples will be empty strings")

    # Find transcripts
    transcripts = sorted(
        [p for p in CLAUDE_PROJECTS.rglob("*.jsonl")
         if "subagents" not in str(p) and p.stat().st_size > 30_000],
        key=lambda p: p.stat().st_size,
        reverse=True,
    )[:n_sessions]

    print(f"Generating contrastive pairs from {len(transcripts)} transcripts...")

    pairs = []
    skipped = 0
    CHUNK_SIZE = 40
    CHUNK_OVERLAP = 10

    for i, transcript_path in enumerate(transcripts):
        session_id = transcript_path.stem
        turns = parse_transcript(transcript_path)
        if len(turns) < 5:
            continue

        # Sample one chunk per transcript (middle portion, most interesting)
        mid = max(0, len(turns) // 2 - CHUNK_SIZE // 2)
        chunk = turns[mid:mid + CHUNK_SIZE]
        conversation = build_conversation(chunk, max_tokens)
        if not conversation.strip():
            continue

        # Generate chosen via claude -p (uses MAX subscription)
        prompt_text = SYSTEM_PROMPT + "\n\n## Conversation\n\n" + conversation
        try:
            r = subprocess.run(
                ["claude", "-p", prompt_text,
                 "--model", "claude-haiku-4-5-20251001"],
                capture_output=True, text=True, timeout=120,
            )
            chosen_raw = r.stdout.strip()
            if not chosen_raw or r.returncode != 0:
                raise RuntimeError(r.stderr.strip() or "empty response")
        except Exception as e:
            print(f"  [warn] claude -p failed for {session_id}: {e}")
            skipped += 1
            continue

        # Filter to valid SSL lines only
        ssl_lines = [l for l in chosen_raw.splitlines() if SSL_PATTERN.match(l.strip())]
        if not ssl_lines:
            skipped += 1
            continue
        chosen = "\n".join(ssl_lines[:8])

        # Build rejected: random memories from pool (different sessions = wrong content)
        if rejection_pool:
            rejected_sample = random.sample(rejection_pool, min(5, len(rejection_pool)))
            rejected = "\n".join(rejected_sample)
        else:
            rejected = "[PATTERN] [unrelated] random→content→here @nowhere"

        pairs.append({
            "prompt": conversation,
            "chosen": chosen,
            "rejected": rejected,
            "session_id": session_id,
        })

        if (i + 1) % 10 == 0:
            print(f"  {i+1}/{len(transcripts)}: {len(pairs)} pairs, {skipped} skipped")

    print(f"\nTotal contrastive pairs: {len(pairs)} ({skipped} skipped)")

    with open(output_path, "w") as f:
        for pair in pairs:
            f.write(json.dumps(pair) + "\n")

    return len(pairs)


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Extract SSL training data")
    parser.add_argument("--output", default="training_data.jsonl")
    parser.add_argument("--min-wisdoms", type=int, default=1)
    parser.add_argument("--max-tokens", type=int, default=MAX_CONV_TOKENS)
    parser.add_argument("--strategy", choices=["existing", "distill", "contrastive"],
                        default="existing",
                        help="existing=use stored wisdoms, distill=run LLM labeling, "
                             "contrastive=DPO pairs via claude API")
    parser.add_argument("--n-sessions", type=int, default=200,
                        help="Number of sessions to process (contrastive strategy)")
    args = parser.parse_args()

    output_path = Path(args.output)
    print(f"Extracting SSL training data → {output_path}")

    if args.strategy == "existing":
        n = generate_pairs_from_transcripts(
            output_path, args.min_wisdoms, args.max_tokens, use_llm=False)
    elif args.strategy == "distill":
        n = generate_pairs_with_distillation(
            output_path, args.min_wisdoms, args.max_tokens)
    else:
        n = generate_contrastive_pairs(
            output_path, args.n_sessions, args.max_tokens)

    print(f"\nDone: {n} training pairs written to {output_path}")
    print(f"File size: {output_path.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()
