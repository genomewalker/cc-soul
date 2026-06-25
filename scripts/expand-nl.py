#!/usr/bin/env python3
"""expand-nl.py — Dual-index expansion: store NL versions of SSL-notation memories.

For each condensed memory (→ | @ notation), calls Ollama to expand to natural
language and stores the result as a new memory with `nl-expansion` tag. Also
writes (ssl, nl) training pairs to JSONL for downstream BGE fine-tuning.

Usage:
    python3 scripts/expand-nl.py [--dry-run] [--realm cc-soul] [--batch 20] [--offset 0]

After each batch, calls `chitta cycle` to flush WAL to searchable HNSW index.
Fine-tune BGE on the output pairs:
    python3 scripts/finetune_bge.py --pairs ~/.claude/training/ssl_nl_pairs.jsonl \\
        --base-model BAAI/bge-large-en-v1.5
"""
import argparse, json, os, re, subprocess, sys, time, urllib.request
from pathlib import Path

OLLAMA_URL   = os.environ.get("OLLAMA_URL", "http://dandygpun01fl.unicph.domain:11434")
OLLAMA_MODEL = os.environ.get("OLLAMA_MODEL", "gemma4:26b")
PAIRS_OUT    = Path(os.path.expanduser("~/.claude/training/ssl_nl_pairs.jsonl"))
CHITTA       = os.environ.get("CHITTA_BIN", os.path.expanduser("~/.claude/bin/chitta"))

SYSTEM = (
    "Convert this compact memory notation into a single natural language retrieval hint. "
    "Output one concise factual sentence in third person. "
    "Preserve all technical details (paths, versions, function names). "
    "Output nothing if it is a job/task/operational/episode record."
)

SSL_RE = re.compile(r"[→\|]|^\[[a-z]")
SKIP_KINDS = {"episode", "alias"}
SKIP_PREFIXES = ("[job]", "[task]", "[thread]", "[agent-task]",
                 "[OPERATIONAL]", "[PROBE]", "turn_assistant")


def is_condensed(text: str) -> bool:
    first = text.split("\n")[0][:300]
    return bool(SSL_RE.search(first)) and len(first) < 250


def already_expanded(tags: list) -> bool:
    return "nl-expansion" in tags


def ollama_expand(ssl_label: str, ollama_url: str = OLLAMA_URL, ollama_model: str = OLLAMA_MODEL) -> str:
    body = json.dumps({
        "model": OLLAMA_MODEL,
        "prompt": ssl_label[:500],
        "system": SYSTEM,
        "stream": False,
        "think": False,
        "options": {"temperature": 0.1, "num_predict": 100},
    }).encode()
    req = urllib.request.Request(
        f"{OLLAMA_URL}/api/generate",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=120) as r:
        resp = json.loads(r.read())
    text = resp.get("response", "").strip()
    for sep in (".", "\n"):
        idx = text.find(sep)
        if 0 < idx < 200:
            text = text[:idx + 1].strip()
            break
    return text[:200] if len(text) >= 8 else ""


def chitta_remember(content: str, realm: str, orig_id: int) -> bool:
    cmd = [CHITTA, "remember",
           "--content", content,
           "--realm", realm,
           "--type", "wisdom",
           "--tags", f"nl-expansion,src:{orig_id}",
           "--visibility", "1",
           "--json"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    return r.returncode == 0


def chitta_cycle() -> None:
    subprocess.run([CHITTA, "cycle", "--json"], capture_output=True, timeout=30)


def list_memories(offset: int, limit: int, realm: str) -> tuple[list[dict], int]:
    cmd = [CHITTA, "list_memories_brief",
           "--limit", str(limit),
           "--offset", str(offset),
           "--json"]
    if realm:
        cmd += ["--realm", realm]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or not r.stdout.strip():
        return [], 0
    data = json.loads(r.stdout)
    if not isinstance(data, dict):
        return [], 0
    return data.get("memories", []), data.get("count", 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run",  action="store_true")
    ap.add_argument("--realm",    default="")
    ap.add_argument("--batch",    type=int, default=20)
    ap.add_argument("--offset",   type=int, default=0)
    ap.add_argument("--limit",    type=int, default=0, help="Max memories to process (0=all)")
    ap.add_argument("--ollama-url",   default=os.environ.get("OLLAMA_URL", "http://dandygpun01fl.unicph.domain:11434"))
    ap.add_argument("--ollama-model", default=os.environ.get("OLLAMA_MODEL", "gemma4:26b"))
    a = ap.parse_args()

    PAIRS_OUT.parent.mkdir(parents=True, exist_ok=True)

    page_size = 100
    offset = a.offset
    processed = expanded = skipped = errors = 0

    print(f"expand-nl: model={OLLAMA_MODEL} url={OLLAMA_URL} realm={a.realm or 'all'} dry={a.dry_run}")

    with PAIRS_OUT.open("a") as pairs_fh:
        while True:
            memories, _total = list_memories(offset, page_size, a.realm)
            if not memories:
                break

            batch_queue = []
            for m in memories:
                processed += 1
                if a.limit and processed > a.limit:
                    break
                content = m.get("content", "")
                kind    = m.get("kind", "")
                tags    = m.get("tags", [])
                realm   = m.get("realm", "brahman")
                mid     = m.get("id", 0)

                if kind in SKIP_KINDS:
                    skipped += 1
                    continue
                if content.startswith(SKIP_PREFIXES):
                    skipped += 1
                    continue
                if already_expanded(tags):
                    skipped += 1
                    continue
                if not is_condensed(content):
                    skipped += 1
                    continue

                batch_queue.append({"ssl": content, "realm": realm, "id": mid})

                if len(batch_queue) >= a.batch:
                    _flush(batch_queue, pairs_fh, a.dry_run, a.ollama_url, a.ollama_model)
                    expanded += len(batch_queue)
                    batch_queue = []
                    if not a.dry_run:
                        chitta_cycle()
                    print(f"  processed={processed} expanded={expanded} skipped={skipped} errors={errors}")

            if batch_queue:
                _flush(batch_queue, pairs_fh, a.dry_run, a.ollama_url, a.ollama_model)
                expanded += len(batch_queue)
                if not a.dry_run:
                    chitta_cycle()

            if a.limit and processed >= a.limit:
                break
            offset += page_size
            time.sleep(0.1)

    print(f"\nDone. processed={processed} expanded={expanded} skipped={skipped} errors={errors}")
    print(f"Pairs written to: {PAIRS_OUT}")
    print(f"\nNext step — fine-tune BGE:")
    print(f"  python3 scripts/finetune_bge.py --pairs {PAIRS_OUT} --base-model BAAI/bge-large-en-v1.5")


def _flush(batch: list[dict], pairs_fh, dry_run: bool, ollama_url: str, ollama_model: str) -> None:
    for item in batch:
        ssl = item["ssl"].split("\n")[0][:400]
        try:
            nl = ollama_expand(ssl, ollama_url, ollama_model)
        except Exception as e:
            sys.stderr.write(f"  [expand] error: {e}\n")
            continue
        if not nl:
            continue
        pairs_fh.write(json.dumps({"query": nl, "pos": ssl, "neg": None}) + "\n")
        pairs_fh.flush()
        if not dry_run:
            chitta_remember(nl, item["realm"], item["id"])


if __name__ == "__main__":
    main()
