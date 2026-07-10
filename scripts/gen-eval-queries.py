#!/usr/bin/env python3
"""Generate eval query pairs for chitta-eval.py.

Samples memories from the live daemon, uses gemma4 (via ollama API) to generate
natural-language queries, verifies retrieval, and saves to hooks/chitta-eval-goldids.json.

Usage:
  python scripts/gen-eval-queries.py [--target 200] [--realm cc-soul] [--out hooks/chitta-eval-goldids.json]
  python scripts/gen-eval-queries.py --append   # add to existing file
"""
import argparse, json, os, random, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).parent.parent
DEFAULT_OUT = ROOT / "hooks" / "chitta-eval-goldids.json"
OLLAMA_URL = os.environ.get("OLLAMA_URL", os.environ.get("OLLAMA_HOST", "http://localhost:11434"))
OLLAMA_MODEL = os.environ.get("CHITTA_EVAL_MODEL", "gemma3:27b")
RECALL_LIMIT = 20
VERIFY_STRATEGY = "hybrid"


def chitta(tool: str, **kwargs) -> dict:
    cmd = ["chitta", tool]
    for k, v in kwargs.items():
        if k == "json":
            cmd.append("--json")
        else:
            cmd += [f"--{k}", str(v)]
    cmd.append("--json")  # always request JSON output
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if not r.stdout.strip():
        return {}
    try:
        result = json.loads(r.stdout)
        return result if isinstance(result, dict) else {}
    except json.JSONDecodeError:
        rows = []
        for line in r.stdout.splitlines():
            line = line.strip()
            if line:
                try:
                    rows.append(json.loads(line))
                except Exception:
                    pass
        return {"results": rows, "memories": rows} if rows else {}


def ollama_generate(prompt: str) -> str:
    import urllib.request
    payload = json.dumps({
        "model": OLLAMA_MODEL,
        "messages": [{"role": "user", "content": prompt}],
        "stream": False,
        "think": False,  # generation-only task; thinking models burn num_predict on reasoning → empty content
        "options": {"num_predict": 60, "temperature": 0.3},
    }).encode()
    req = urllib.request.Request(f"{OLLAMA_URL}/api/chat", data=payload,
                                  headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return json.loads(resp.read())["message"]["content"].strip()
    except Exception:
        return ""


_SAMPLE_SEEDS = [
    "architecture decision", "bug fix approach", "workflow pattern",
    "data processing", "configuration", "performance optimization",
    "error handling", "deployment", "testing strategy", "code structure",
    "memory system", "recall quality", "distillation", "embedding model",
    "file provenance", "job submission", "cluster compute", "database schema",
    "protein structure", "sequence alignment", "taxonomic classification",
    "phylogenetic analysis", "ancient DNA", "damage estimation",
]

def sample_memories(realm: str | None, n: int) -> list[dict]:
    seen_ids: set[str] = set()
    memories: list[dict] = []
    seeds = _SAMPLE_SEEDS.copy()
    random.shuffle(seeds)
    per_seed = max(10, n // len(seeds) + 1)
    for seed in seeds:
        if len(memories) >= n * 2:
            break
        kwargs: dict = {"query": seed, "limit": per_seed, "strategy": "hybrid"}
        if realm:
            kwargs["realm"] = realm
        result = chitta("recall", **kwargs)
        for r in result.get("results", []):
            mid = str(r.get("id", ""))
            if mid and mid not in seen_ids:
                seen_ids.add(mid)
                # Normalize field names
                memories.append({
                    "id": mid,
                    "kind": r.get("kind"),
                    "realm": r.get("realm", ""),
                    "content": r.get("text", r.get("content", "")),
                    "preview": r.get("text", "")[:100],
                })
    # Filter episodes (kind_episode=0.15 penalty memories are low-value as eval targets)
    useful = [m for m in memories if m.get("kind") not in ("episode", None)]
    if len(useful) < n // 2:
        useful = memories  # fallback: take everything
    random.shuffle(useful)
    return useful[:n]


def make_query(memory: dict) -> str | None:
    content = memory.get("content", memory.get("preview", ""))
    if not content or len(content) < 20:
        return None
    snippet = content[:300]
    prompt = (
        f"Memory: {snippet}\n\n"
        "Write one short question (5-12 words) a user would ask to retrieve this. "
        "Return ONLY the question."
    )
    q = ollama_generate(prompt)
    # Clean up
    q = re.sub(r'^["\'`]|["\'`]$', '', q.strip())
    q = q.split("\n")[0].strip()
    if len(q) < 5 or len(q) > 200:
        return None
    return q


def verify(query: str, memory_id: str | int) -> bool:
    mid = str(memory_id)
    result = chitta("recall", query=query, limit=RECALL_LIMIT, strategy=VERIFY_STRATEGY)
    results = result.get("results", [])
    return any(str(r.get("id")) == mid for r in results)


def make_multihop(memories: list[dict]) -> list[dict]:
    """Generate multi-hop pairs: find memories in same realm, ask gemma4 for bridging query."""
    by_realm: dict[str, list[dict]] = {}
    for m in memories:
        r = m.get("realm", "")
        by_realm.setdefault(r, []).append(m)
    pairs = []
    for realm, mems in by_realm.items():
        if len(mems) < 2:
            continue
        for _ in range(min(3, len(mems) // 2)):
            a, b = random.sample(mems, 2)
            ca = a.get("content", a.get("preview", ""))[:300]
            cb = b.get("content", b.get("preview", ""))[:300]
            if not ca or not cb:
                continue
            prompt = (
                f"Given these two related memories:\n\nA: {ca}\n\nB: {cb}\n\n"
                "Write ONE natural-language question (8-15 words) whose answer requires "
                "knowing BOTH memories. Return ONLY the question."
            )
            q = ollama_generate(prompt)
            q = re.sub(r'^["\'`]|["\'`]$', '', q.split("\n")[0].strip())
            if 5 < len(q) < 200:
                pairs.append({"query": q, "ids": [str(a["id"]), str(b["id"])],
                               "type": "multihop", "realm": realm})
    return pairs


def make_abstention(n: int = 10) -> list[dict]:
    """Queries that should return nothing relevant — tests abstention."""
    prompts = [
        "how to configure kubernetes ingress for rust services",
        "what is the mating season of arctic foxes",
        "recipe for sourdough bread with spelt flour",
        "history of the Byzantine Empire trade routes",
        "how does CRISPR base editing differ from prime editing",
        "best practices for React server components in Next.js 15",
        "how to migrate from MySQL 5.7 to PostgreSQL 16",
        "what caused the 1929 stock market crash",
        "how does gradient descent find local minima in neural networks",
        "how to train a GAN for image super-resolution",
    ]
    return [{"query": p, "ids": [], "type": "abstention"} for p in prompts[:n]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", type=int, default=150,
                    help="Number of single-hop pairs to generate (default: 150)")
    ap.add_argument("--realm", default=None,
                    help="Restrict sampling to a realm (e.g. cc-soul)")
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--append", action="store_true",
                    help="Append to existing file instead of overwriting")
    ap.add_argument("--multihop", type=int, default=20,
                    help="Number of multi-hop pairs to attempt (default: 20)")
    ap.add_argument("--abstention", type=int, default=10)
    ap.add_argument("--no-verify", action="store_true",
                    help="Skip retrieval verification (faster, less accurate)")
    args = ap.parse_args()

    out_path = Path(args.out)

    existing: dict = {"version": 1, "ids": {}, "meta": []}
    if args.append and out_path.exists():
        existing = json.loads(out_path.read_text())

    print(f"Sampling memories (target={args.target}, realm={args.realm or 'all'})...")
    memories = sample_memories(args.realm, args.target * 4)
    print(f"Sampled {len(memories)} candidate memories.")

    ids: dict = existing.get("ids", {})
    meta: list = existing.get("meta", [])
    generated = 0
    verified = 0
    skipped = 0

    for i, mem in enumerate(memories):
        if generated >= args.target:
            break
        mid = str(mem["id"])
        content = mem.get("content", mem.get("preview", ""))
        if not content:
            skipped += 1
            continue

        print(f"  [{i+1}/{len(memories)}] generating query for memory {mid[:12]}...", end="", flush=True)
        query = make_query(mem)
        if not query:
            print(" skip (bad query)")
            skipped += 1
            continue

        if not args.no_verify:
            ok = verify(query, mid)
            if not ok:
                print(f" ✗ not retrieved: {query[:60]}")
                skipped += 1
                continue
            verified += 1

        label = query[:60].lower().replace(" ", "_")
        # Avoid duplicate labels
        if label in ids:
            label = f"{label}_{mid[:6]}"
        ids[label] = [mid]
        meta.append({"query": query, "label": label, "memory_id": mid,
                     "realm": mem.get("realm", ""), "type": "single_hop"})
        generated += 1
        print(f" ✓ [{generated}/{args.target}] {query[:60]}")
        time.sleep(0.1)  # avoid hammering daemon

    # Multi-hop pairs
    if args.multihop > 0:
        print(f"\nGenerating multi-hop pairs (target={args.multihop})...")
        mh_candidates = sample_memories(args.realm, 200)
        mh_pairs = make_multihop(mh_candidates)
        for p in mh_pairs[:args.multihop]:
            if not p["ids"]:
                continue
            # Verify: at least one gold ID retrieved
            if not args.no_verify:
                result = chitta("recall", query=p["query"], limit=RECALL_LIMIT,
                                strategy=VERIFY_STRATEGY)
                retrieved = {str(r.get("id")) for r in result.get("results", [])}
                gold_hit = [gid for gid in p["ids"] if gid in retrieved]
                if not gold_hit:
                    continue
                p["ids"] = gold_hit  # only keep retrievable golds
            label = f"mh_{p['query'][:40].lower().replace(' ', '_')}"
            ids[label] = p["ids"]
            meta.append({"query": p["query"], "label": label,
                          "memory_ids": p["ids"], "realm": p["realm"], "type": "multihop"})
            print(f" ✓ multihop: {p['query'][:60]}")

    # Abstention pairs
    abs_pairs = make_abstention(args.abstention)
    for p in abs_pairs:
        label = f"abs_{p['query'][:40].lower().replace(' ', '_')}"
        ids[label] = []  # empty = no gold; eval runner scores abstention separately
        meta.append({"query": p["query"], "label": label, "type": "abstention"})

    output = {"version": 1, "ids": ids, "meta": meta}
    out_path.write_text(json.dumps(output, indent=2))

    print(f"\nDone. {generated} single-hop, {len([m for m in meta if m['type']=='multihop'])} multihop, "
          f"{len(abs_pairs)} abstention pairs.")
    print(f"Verified: {verified}/{generated}. Skipped: {skipped}.")
    print(f"Saved → {out_path}")


if __name__ == "__main__":
    main()
