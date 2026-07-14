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
    # No `except: return ""`. A dead ollama used to make every make_query() return None,
    # so the generator printed "skip (bad query)" 600 times and wrote an EMPTY gold set
    # while exiting 0. A generator that cannot generate must not look like one that found
    # nothing worth keeping.
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read())["message"]["content"].strip()


def store_size(realm: str | None) -> int:
    r = subprocess.run(["chitta", "hygiene_stats"], capture_output=True, text=True, timeout=60)
    m = re.search(r"total memories\s*:\s*(\d+)", r.stdout)
    if not m:
        raise RuntimeError("cannot read store size from `chitta hygiene_stats`")
    return int(m.group(1))


def sample_memories(realm: str | None, n: int) -> list[dict]:
    """Uniform random sample over the WHOLE store, via an enumeration path.

    The previous implementation drew candidates by calling `chitta recall` on 24 hard-coded
    seed topics. That made the eval population a function of the ranker being evaluated: a
    memory recall never surfaces could never become an eval target, so recall misses were
    unmeasurable by construction. `list_memories_brief` paginates by offset and does no
    ranking, so it is the only sampling frame here that the system under test cannot bias.
    """
    total = store_size(realm)
    offsets = random.sample(range(total), min(n, total))
    memories: list[dict] = []
    for off in offsets:
        cmd = ["chitta", "list_memories_brief", "--limit", "1", "--offset", str(off)]
        if realm:
            cmd += ["--realm", realm]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        for line in r.stdout.splitlines():
            if not line.startswith("{"):
                continue
            m = json.loads(line)
            memories.append({
                "id": str(m.get("id", "")),
                "kind": m.get("kind"),
                "realm": m.get("realm", ""),
                "content": m.get("content", ""),
                "preview": m.get("preview", "")[:100],
            })
    return memories


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


_STOP = {"how", "what", "does", "the", "for", "with", "from", "and", "best", "practices",
         "differ", "caused", "find", "train", "configure", "migrate", "recipe", "history"}


def certify_negatives(prompts: list[str], store_path: str) -> list[str]:
    """Keep only negatives with no lexical witness in the store.

    A negative is only a negative if the store genuinely cannot answer it. Checking that with
    `recall` would be circular — a ranker that misses the one relevant memory would certify the
    negative and then be scored 1.0 for abstaining on it. So we check against the enumerated
    dump instead, which does no ranking.

    ceiling: lexical overlap is a proxy for semantic relevance — a memory could be relevant
    without sharing any content word, so this rejects unsafe negatives but cannot prove safety.
    upgrade: embed the dump once and reject any negative with a near neighbour above threshold.
    """
    memories = [set(re.findall(r"[a-z0-9]{4,}", json.loads(ln)["content"].lower()))
                for ln in Path(store_path).read_text().splitlines() if ln.startswith("{")]
    kept = []
    for p in prompts:
        terms = {w for w in re.findall(r"[a-z0-9]{4,}", p.lower()) if w not in _STOP}
        # Match whole WORDS, not substrings. Substring matching rejected every negative here:
        # "rust" hit inside "trust", so a memory about eco-friendly detergent was returned as
        # proof the store could answer a question about kubernetes.
        witness = next((m for m in memories if len(terms & m) >= 2), None)
        if witness:
            print(f"  [negative REJECTED — store can answer it] {p}\n"
                  f"      shared terms: {sorted(terms & witness)}")
            continue
        kept.append(p)
    return kept


def make_abstention(n: int = 10, store_path: str | None = None) -> list[dict]:
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
    # An uncertified negative is worse than no negative: it scores the daemon 0 for correctly
    # answering a question the store CAN answer. Refuse to emit them silently.
    if store_path:
        prompts = certify_negatives(prompts, store_path)
    else:
        print("WARNING: --store not given; abstention negatives are UNCERTIFIED — "
              "some may be answerable from the store, which would score correct answers as failures")
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
    ap.add_argument("--sample-out",
                    help="Write the uniform sample as jsonl and exit. Query synthesis is then "
                         "someone else's job; feed the result back with --queries-in.")
    ap.add_argument("--queries-in",
                    help="jsonl of {memory_id, query} to use instead of calling ollama. Lets the "
                         "sampling frame and the query writer be different systems.")
    ap.add_argument("--store",
                    help="jsonl dump from scripts/dump-store.py. Required to certify abstention "
                         "negatives against what the store actually contains.")
    args = ap.parse_args()

    out_path = Path(args.out)

    if args.sample_out:
        sample = sample_memories(args.realm, args.target)
        Path(args.sample_out).write_text("\n".join(json.dumps(m) for m in sample) + "\n")
        print(f"wrote {len(sample)} memories to {args.sample_out}")
        return

    supplied: dict[str, str] = {}
    realms: dict[str, str] = {}
    if args.queries_in:
        for line in Path(args.queries_in).read_text().splitlines():
            if line.strip():
                d = json.loads(line)
                supplied[str(d["memory_id"])] = d["query"]
                realms[str(d["memory_id"])] = d.get("realm", "")

    existing: dict = {"version": 1, "ids": {}, "meta": []}
    if args.append and out_path.exists():
        existing = json.loads(out_path.read_text())

    if args.queries_in:
        memories = [{"id": mid, "realm": realms.get(mid, ""), "content": q}
                    for mid, q in supplied.items()]
        print(f"Using {len(memories)} pre-written queries from {args.queries_in}.")
    else:
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
        query = supplied.get(mid) or make_query(mem)
        if not query:
            print(" skip (bad query)")
            skipped += 1
            continue

        # A query the daemon cannot answer is the MOST informative query in the set.
        # This used to `continue` — discarding it — which meant a recall miss could never
        # enter the denominator and nDCG could not fall when recall got worse. The verify
        # result is now recorded as data, never used as a filter.
        retrieved_at_gen = None
        if not args.no_verify:
            retrieved_at_gen = verify(query, mid)
            if retrieved_at_gen:
                verified += 1
            else:
                print(f" [miss@gen — KEPT] {query[:50]}")

        label = query[:60].lower().replace(" ", "_")
        # Avoid duplicate labels
        if label in ids:
            label = f"{label}_{mid[:6]}"
        ids[label] = [mid]
        meta.append({"query": query, "label": label, "memory_id": mid,
                     "realm": mem.get("realm", ""), "type": "single_hop",
                     "retrieved_at_gen": retrieved_at_gen})
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
            # Gold is BOTH memories the query was built from. It is not "whichever of them
            # recall happened to return" — that was `p["ids"] = gold_hit`, which trimmed the
            # gold set to fit the answer, so a 2-of-2 multihop that only found 1 was silently
            # rescored as a perfect 1-of-1. A multihop query that retrieves neither is kept.
            retrieved_at_gen = None
            if not args.no_verify:
                result = chitta("recall", query=p["query"], limit=RECALL_LIMIT,
                                strategy=VERIFY_STRATEGY)
                retrieved = {str(r.get("id")) for r in result.get("results", [])}
                retrieved_at_gen = sum(1 for gid in p["ids"] if gid in retrieved)
            label = f"mh_{p['query'][:40].lower().replace(' ', '_')}"
            ids[label] = p["ids"]
            meta.append({"query": p["query"], "label": label,
                          "memory_ids": p["ids"], "realm": p["realm"], "type": "multihop",
                          "retrieved_at_gen": retrieved_at_gen})
            print(f" ✓ multihop: {p['query'][:60]}")

    # Abstention pairs
    abs_pairs = make_abstention(args.abstention, args.store)
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
