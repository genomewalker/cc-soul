#!/usr/bin/env python3
"""
hint_realtime.py — Fire-and-forget: extract hints from recent user turns.

Called by prompt-core.sh every HINT_INTERVAL turns in background.
Reads the last N user messages from the current transcript, runs them
through the installed hint model (llama_cpp, no Ollama), and stores
non-empty results via chitta remember.

Usage (from hook):
    python3 hint_realtime.py \
        --transcript PATH \
        --session SESSION_ID \
        --turns N \
        [--model PATH] \
        [--chitta-bin PATH]
"""

import argparse, hashlib, json, os, re, signal, subprocess, sys
from pathlib import Path

DEFAULT_MODEL  = Path.home() / ".claude/models/chitta-hint-qwen-q4_k_m.gguf"
DEFAULT_CHITTA = Path.home() / ".claude/bin/chitta"
MAX_TOKENS     = 80
N_CTX          = 512

SYSTEM = ("Extract a single concise retrieval hint from the message. "
          "Cover: personal preferences, tech choices, developer workflows, "
          "domain expertise, project facts, and recurring patterns. "
          "If nothing factual is present, output nothing.")

NOISE_PREFIXES = (
    "base directory", "this session is being continued", "[request interrupted",
    "<local-command", "<command-name", "<system-reminder", "<task-notification",
    "you are **claude:", "userpromptsub", "# /",
)

def _is_noise(text: str) -> bool:
    t = text.strip().lower()
    if len(t) < 20:
        return True
    for p in NOISE_PREFIXES:
        if t.startswith(p):
            return True
    return False


def _last_user_turns(transcript: Path, n: int) -> list[str]:
    turns = []
    try:
        for line in transcript.read_text(errors="replace").splitlines():
            try:
                ev = json.loads(line)
            except Exception:
                continue
            if ev.get("type") != "user":
                continue
            msg = ev.get("message", {})
            if msg.get("role") != "user":
                continue
            content = msg.get("content", "")
            blocks = [content] if isinstance(content, str) else content
            for block in blocks:
                if isinstance(block, str):
                    text = block
                elif isinstance(block, dict) and block.get("type") == "text":
                    text = block.get("text", "")
                else:
                    continue
                text = text.strip()
                if text and not _is_noise(text):
                    turns.append(text[:500])
    except Exception as e:
        sys.stderr.write(f"[hint_realtime] transcript read error: {e}\n")
    return turns[-n:]


def _extract_hint(llm, text: str) -> str:
    prompt = (f"<|im_start|>system\n{SYSTEM}<|im_end|>\n"
              f"<|im_start|>user\n{text}<|im_end|>\n"
              f"<|im_start|>assistant\n")
    out = llm(prompt, max_tokens=MAX_TOKENS, temperature=0.0, stop=["<|im_end|>"])
    hint = out["choices"][0]["text"].strip()
    if not hint or hint in ("-", "--") or len(hint) < 6:
        return ""
    return hint[:200]


def _remember(hint: str, session_id: str, chitta_bin: Path) -> None:
    content = f"[hint:realtime] {hint}"
    subprocess.run(
        [str(chitta_bin), "remember",
         "--content", content,
         "--tags", "retrieval_hint,hint:realtime",
         "--type", "signal"],
        capture_output=True, timeout=10,
    )


def _hint_hash(hint: str) -> str:
    return hashlib.md5(hint.encode()).hexdigest()[:8]


def _load_seen(seen_file: Path) -> set:
    if not seen_file.exists():
        return set()
    try:
        return set(seen_file.read_text().splitlines())
    except Exception:
        return set()


def _append_seen(seen_file: Path, h: str) -> None:
    try:
        with seen_file.open("a") as f:
            f.write(h + "\n")
    except Exception:
        pass


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--transcript", required=True)
    ap.add_argument("--session",    required=True)
    ap.add_argument("--turns",      type=int, default=5)
    ap.add_argument("--model",      default=str(DEFAULT_MODEL))
    ap.add_argument("--chitta-bin", default=str(DEFAULT_CHITTA))
    ap.add_argument("--mind-path",  default=str(Path.home() / ".claude/mind"))
    args = ap.parse_args()

    transcript = Path(args.transcript)
    if not transcript.exists():
        sys.exit(0)

    turns = _last_user_turns(transcript, args.turns)
    if not turns:
        sys.exit(0)

    mind_path = Path(args.mind_path)
    seen_file = mind_path / f".hint_seen_{args.session}"
    seen = _load_seen(seen_file)

    try:
        from llama_cpp import Llama
    except ImportError:
        sys.stderr.write("[hint_realtime] llama_cpp not available\n")
        sys.exit(1)

    # Timeout guard: 30s total for model load + all inference
    def _timeout_handler(signum, frame):
        sys.exit(0)

    signal.signal(signal.SIGALRM, _timeout_handler)
    signal.alarm(30)

    llm = Llama(model_path=args.model, n_ctx=N_CTX, n_gpu_layers=0, verbose=False)

    stored = 0
    for text in turns:
        hint = _extract_hint(llm, text)
        if not hint:
            continue
        h = _hint_hash(hint)
        if h in seen:
            continue
        _remember(hint, args.session, Path(args.chitta_bin))
        _append_seen(seen_file, h)
        seen.add(h)
        stored += 1
        sys.stderr.write(f"[hint_realtime] stored: {hint[:80]}\n")

    signal.alarm(0)
    sys.stderr.write(f"[hint_realtime] {stored}/{len(turns)} hints stored\n")


if __name__ == "__main__":
    main()
