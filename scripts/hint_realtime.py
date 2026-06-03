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

import argparse, fcntl, hashlib, json, os, re, signal, subprocess, sys, time
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


BACKEND = "subprocess"

PREFILTER_KEYWORDS = re.compile(
    r"\b(prefer|rather|instead|use|switch to|go with|let'?s|decided|chose|chosen|"
    r"avoid|always|never|want|default|don'?t|stick with)\b", re.I)


def _now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _log_metric(mind_path: Path, rec: dict) -> None:
    try:
        with (mind_path / "hint_metrics.jsonl").open("a") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    except Exception:
        pass


def _sha256_prefix(path: Path, n: int = 16):
    try:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()[:n]
    except Exception:
        return None


def _model_fingerprint(model_path: Path, mind_path: Path) -> dict:
    """Stat-keyed fingerprint. Hashes the (850 MB) model ONLY when its
    path+size+mtime changes — never on every fire."""
    try:
        st = model_path.stat()
    except OSError:
        return {"size": None, "mtime": None, "sha256_prefix": None}
    key = f"{model_path}:{st.st_size}:{int(st.st_mtime)}"
    cache_file = mind_path / ".hint_model_fp.json"
    try:
        cache = json.loads(cache_file.read_text())
    except Exception:
        cache = {}
    if cache.get("key") == key and cache.get("sha256_prefix"):
        sha = cache["sha256_prefix"]
    else:
        sha = _sha256_prefix(model_path)
        try:
            cache_file.write_text(json.dumps({"key": key, "sha256_prefix": sha}))
        except Exception:
            pass
    return {"size": st.st_size, "mtime": int(st.st_mtime), "sha256_prefix": sha}


def _acquire_lock(mind_path: Path):
    """Non-blocking in-flight guard: one realtime fire at a time. Returns
    (fd, None) on success (keep fd open for the process lifetime), or
    (None, reason) where reason is 'open_error' (lock file couldn't be opened)
    or 'contended' (another fire holds it). Bridge-era only — removed at the
    chitta-hintd cutover."""
    try:
        fd = os.open(str(mind_path / ".hint_realtime.lock"), os.O_CREAT | os.O_RDWR, 0o644)
    except OSError:
        return (None, "open_error")
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return (fd, None)
    except OSError:
        os.close(fd)
        return (None, "contended")


def _would_prefilter_skip(text: str) -> bool:
    """SHADOW pre-filter (PR1): logged, never acted on. Skip a turn only when it
    is short AND a pure question AND carries no decision/preference keyword."""
    t = text.strip()
    short = len(t) < 40
    pure_q = t.endswith("?") and "." not in t and "\n" not in t
    no_kw = PREFILTER_KEYWORDS.search(t) is None
    return short and pure_q and no_kw
def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--transcript", required=True)
    ap.add_argument("--session",    required=True)
    ap.add_argument("--turns",      type=int, default=5)
    ap.add_argument("--model",      default=str(DEFAULT_MODEL))
    ap.add_argument("--chitta-bin", default=str(DEFAULT_CHITTA))
    ap.add_argument("--mind-path",  default=str(Path.home() / ".claude/mind"))
    args = ap.parse_args()

    mind_path = Path(args.mind_path)
    t0 = time.monotonic()
    fp: dict = {}
    _term = {"done": False}
    TERMINAL = {"no_turns", "inflight_skip", "lock_open_error",
                "llama_unavailable", "timeout", "error", "stored", "empty"}

    def _emit(outcome, hints_extracted=0, model_load_ms=None,
              turn_prefix=None, shadow=False):
        # Terminal outcomes are emitted at most once; guards the narrow
        # alarm(0)-vs-queued-SIGALRM race so metric counts stay exact.
        if outcome in TERMINAL:
            if _term["done"]:
                return
            _term["done"] = True
        rec = {
            "ts": _now_iso(),
            "backend": BACKEND,
            "outcome": outcome,
            "elapsed_ms": int((time.monotonic() - t0) * 1000),
            "model_load_ms": model_load_ms,
            "model_sha256_prefix": fp.get("sha256_prefix"),
            "hints_extracted": hints_extracted,
        }
        if turn_prefix is not None:
            rec["turn_prefix"] = turn_prefix.strip()[:80]
        if shadow:
            rec["shadow"] = True
        _log_metric(mind_path, rec)

    transcript = Path(args.transcript)
    if not transcript.exists():
        _emit("no_turns")
        sys.exit(0)

    # In-flight lock: collapse overlapping background fires into one cold load.
    lock_fd, lock_err = _acquire_lock(mind_path)
    if lock_fd is None:
        _emit("lock_open_error" if lock_err == "open_error" else "inflight_skip")
        sys.exit(0)

    fp = _model_fingerprint(Path(args.model), mind_path)

    turns = _last_user_turns(transcript, args.turns)
    if not turns:
        _emit("no_turns")
        sys.exit(0)

    # Shadow pre-filter: record would-skip turns; do NOT act on them.
    for text in turns:
        if _would_prefilter_skip(text):
            _emit("prefilter_skip", turn_prefix=text, shadow=True)

    seen_file = mind_path / f".hint_seen_{args.session}"
    seen = _load_seen(seen_file)

    try:
        from llama_cpp import Llama
    except ImportError:
        sys.stderr.write("[hint_realtime] llama_cpp not available\n")
        _emit("llama_unavailable")
        sys.exit(1)

    ctx = {"model_load_ms": None, "hints": 0}

    # Timeout guard (soft): 30s for model load + all inference. A native
    # llama_cpp hang holds the GIL and defers this; the hook's `timeout -k 5 35`
    # wrapper is the hard backstop that reaps the process and the flock.
    def _timeout_handler(signum, frame):
        _emit("timeout", hints_extracted=ctx["hints"],
              model_load_ms=ctx["model_load_ms"])
        sys.exit(0)

    signal.signal(signal.SIGALRM, _timeout_handler)
    signal.alarm(30)

    try:
        _load0 = time.monotonic()
        llm = Llama(model_path=args.model, n_ctx=N_CTX, n_gpu_layers=0,
                    n_threads=2, verbose=False)
        ctx["model_load_ms"] = int((time.monotonic() - _load0) * 1000)
    except Exception as e:
        signal.alarm(0)
        sys.stderr.write(f"[hint_realtime] model load failed: {e}\n")
        _emit("error")
        sys.exit(1)

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
        ctx["hints"] = stored
        sys.stderr.write(f"[hint_realtime] stored: {hint[:80]}\n")

    signal.alarm(0)
    _emit("stored" if stored else "empty", hints_extracted=stored,
          model_load_ms=ctx["model_load_ms"])
    sys.stderr.write(f"[hint_realtime] {stored}/{len(turns)} hints stored\n")
if __name__ == "__main__":
    main()
