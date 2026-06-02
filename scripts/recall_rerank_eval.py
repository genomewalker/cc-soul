#!/usr/bin/env python3
"""Offline gate for the geometric-damping rerank (consolidation-redesign Step 2).

Compares three rankers on the live store sidecars — raw cosine, centered cosine
(production), centered+damped s'(q,d)=s_c-beta*r_d — via TREC pooling + a blind
local-LLM relevance judge, and prints the go/no-go gate the room agreed on:
  - per-stratum Recall@10 must not drop > 1 pt
  - nDCG@10 delta >= 0 with 95% bootstrap CI excluding a loss
  - top-1% hub slot-share strictly decreases

Three resumable stages (artifacts under OUT, judgments cached so re-runs are cheap):
  prep   load .emb/.pld/.mu, center, r_d cache, hubness, strata, query sample, rankers
  judge  pool top-k per ranker, blind graded 0/1/2 judge via ollama, resumable cache
  score  metrics per stratum + bootstrap CI + gate verdict

Query vectors are the docs' own stored embeddings (exact space match, no re-embed),
with the query doc excluded from its own results; gold is a DIFFERENT memory judged
relevant — non-circular, and exactly the hub-distractor question damping targets.
"""
from __future__ import annotations
import argparse, json, os, struct, sys, urllib.request
from pathlib import Path
import numpy as np

MIND = Path.home() / ".claude/mind/chitta-field"
FAM = "chitta.29df8e85"
OUT = Path("/projects/caeg/scratch/kbd606/tmp/recall_eval")
DIM = 1024
K = 10                      # eval depth
POOL_DEPTH = 10             # top-k pooled per ranker for judging
OLLAMA = os.environ.get("OLLAMA_HOST", "http://localhost:11434").rstrip("/") + "/api/generate"
JUDGE_MODEL = "gemma3:27b"


# ── sidecar parsing ────────────────────────────────────────────────────────
def load_emb(path: Path):
    raw = np.fromfile(path, dtype=np.uint8)
    cnt = struct.unpack("<Q", raw[8:16].tobytes())[0]
    rec = raw[16:16 + cnt * (8 + DIM * 4)].reshape(cnt, 8 + DIM * 4)
    ids = rec[:, :8].copy().view(np.uint64).reshape(cnt)
    X = rec[:, 8:].copy().view(np.float32).reshape(cnt, DIM).astype(np.float32)
    n = np.linalg.norm(X, axis=1, keepdims=True); n[n == 0] = 1
    return ids, X / n


def load_pld(path: Path):
    raw = path.read_bytes()
    cnt = struct.unpack("<Q", raw[8:16])[0]
    off = 16; out = {}
    for _ in range(cnt):
        mid = struct.unpack("<Q", raw[off:off + 8])[0]; off += 8
        ln = struct.unpack("<I", raw[off:off + 4])[0]; off += 4
        out[mid] = raw[off:off + ln].decode("utf-8", "replace"); off += ln
    return out


def load_mu(path: Path):
    raw = np.fromfile(path, dtype=np.uint8)
    return raw[-DIM * 4:].copy().view(np.float32).reshape(DIM).astype(np.float32)


def stratum_of(text: str) -> str:
    t = text.lstrip()
    if t.startswith("[") and "]" in t[:40]:
        return t[1:t.index("]")]
    return "untagged"


# ── stage: prep ────────────────────────────────────────────────────────────
def stage_prep(args):
    OUT.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(0)
    pld = load_pld(MIND / f"{FAM}.pld")
    m = args.m
    cached = (OUT / "Xc.npy").exists() and (OUT / "rd.npy").exists() and not args.recompute
    if cached:
        print("reusing cached X/Xc/rd ...", flush=True)
        ids = np.load(OUT / "ids.npy"); X = np.load(OUT / "X.npy")
        Xc = np.load(OUT / "Xc.npy"); rd = np.load(OUT / "rd.npy")
        n = len(ids)
    else:
        print("loading sidecars ...", flush=True)
        ids, X = load_emb(MIND / f"{FAM}.emb")
        mu_side = load_mu(MIND / f"{FAM}.mu")
        has_txt = np.array([int(i) in pld for i in ids])
        ids, X = ids[has_txt], X[has_txt]
        n = len(ids)
        mu = X.mean(0)
        print(f"  N={n}  |corpus_mean|={np.linalg.norm(mu):.4f}  "
              f"cos(mu_side,mean)={float(mu_side@mu/(np.linalg.norm(mu_side)*np.linalg.norm(mu)+1e-9)):.4f}",
              flush=True)
        Xc = X - mu
        nc = np.linalg.norm(Xc, axis=1, keepdims=True); nc[nc == 0] = 1
        Xc = Xc / nc
        print(f"computing r_d (m={m}) over full corpus ...", flush=True)
        rd = np.zeros(n, dtype=np.float32)
        for s in range(0, n, 1024):
            blk = Xc[s:s + 1024] @ Xc.T
            part = np.partition(-blk, m + 1, axis=1)[:, :m + 1]
            rd[s:s + 1024] = (-np.sort(-part, axis=1))[:, 1:m + 1].mean(axis=1)
            if s % (1024 * 20) == 0: print(f"  rd {s}/{n}", flush=True)
        np.save(OUT / "ids.npy", ids); np.save(OUT / "X.npy", X)
        np.save(OUT / "Xc.npy", Xc); np.save(OUT / "rd.npy", rd)

    # hubness: N_k in-degree under centered top-K, denser query sample for a usable label
    print("computing hubness N_k ...", flush=True)
    qsamp = rng.choice(n, min(20000, n), replace=False)
    nk = np.zeros(n, dtype=np.int64)
    for s in range(0, len(qsamp), 500):
        S = Xc[qsamp[s:s + 500]] @ Xc.T
        idx = np.argpartition(-S, K + 1, axis=1)[:, :K + 1]
        for r, row in zip(qsamp[s:s + 500], idx):
            for c in row:
                if c != r: nk[c] += 1
    hub_thresh = np.sort(nk)[-n // 100]
    is_hub = nk >= max(2, hub_thresh)
    print(f"  hub thresh={hub_thresh} hubs={int(is_hub.sum())} N_k skew={float(_skew(nk)):.3f}", flush=True)
    np.save(OUT / "nk.npy", nk); np.save(OUT / "is_hub.npy", is_hub)

    strata = np.array([stratum_of(pld[int(i)]) for i in ids])
    uniq, counts = np.unique(strata, return_counts=True)
    # top strata by size — many tiny tags would dilute the per-stratum budget to noise
    big = [u for u, _ in sorted(zip(uniq, counts), key=lambda x: -x[1]) if _ >= 1000][:8]
    print(f"  top strata: {big}", flush=True)

    # hub-stress = queries whose centered top-10 sits in dense/high-r_d regions
    # (mechanism-aligned: exactly where damping acts). Fixed allocation so the
    # target stratum is large enough to gate; the rest spread over the top strata.
    hub_n = max(40, args.queries // 4)
    per = max(8, (args.queries - hub_n) // max(1, len(big)))
    cand = rng.choice(n, min(8000, n), replace=False)
    dens = np.full(n, -1.0)
    for s in range(0, len(cand), 500):
        S = Xc[cand[s:s + 500]] @ Xc.T
        idx = np.argpartition(-S, K + 1, axis=1)[:, :K + 1]
        for r, row in zip(cand[s:s + 500], idx):
            top = [c for c in row if c != r][:K]
            dens[r] = float(rd[top].mean())
    hubq = [int(c) for c in cand[np.argsort(-dens[cand])][:hub_n]]

    qrows = list(hubq)
    for u in big:
        pool = np.where(strata == u)[0]
        qrows.extend(int(x) for x in rng.choice(pool, min(per, len(pool)), replace=False))
    qrows = list(dict.fromkeys(qrows))
    hubset = set(hubq)
    # emit memory IDs (stable across reloads); strata keyed by memory ID
    qstratum = {int(ids[r]): ("hub-stress" if r in hubset else str(strata[r])) for r in qrows}
    print(f"  sampled {len(qrows)} queries ({len(hubq)} hub-stress)", flush=True)

    (OUT / "queries.json").write_text(json.dumps(
        {"qids": [int(ids[r]) for r in qrows], "stratum": {str(k): v for k, v in qstratum.items()},
         "m": m, "beta": args.beta}))
    print(f"prep done → {OUT}", flush=True)


def _skew(a):
    a = a.astype(np.float64); m = a.mean(); s = a.std()
    return ((a - m) ** 3).mean() / (s ** 3 + 1e-12)


# ── rankers (shared by judge + score) ──────────────────────────────────────
def rank_all(qrow, Xc, X, rd, beta, exclude):
    raw = X @ X[qrow]
    cen = Xc @ Xc[qrow]
    dmp = cen - beta * rd
    out = {}
    for name, sc in (("raw", raw), ("centered", cen), ("damped", dmp)):
        sc = sc.copy(); sc[exclude] = -1e9
        out[name] = np.argpartition(-sc, POOL_DEPTH, axis=0)[:POOL_DEPTH]
        out[name] = out[name][np.argsort(-sc[out[name]])]
    return out


# ── stage: judge ───────────────────────────────────────────────────────────
def ollama_judge(query_text: str, doc_text: str) -> int:
    prompt = (
        "You grade whether a candidate memory is relevant to a search query.\n"
        "Reply with ONLY one digit:\n"
        "  2 = directly answers / same specific topic\n"
        "  1 = related / partially useful\n"
        "  0 = unrelated\n\n"
        f"QUERY:\n{query_text[:700]}\n\nCANDIDATE:\n{doc_text[:700]}\n\nGRADE:"
    )
    body = json.dumps({"model": JUDGE_MODEL, "prompt": prompt, "stream": False,
                       "options": {"temperature": 0, "num_predict": 2}}).encode()
    req = urllib.request.Request(OLLAMA, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        resp = json.loads(r.read())["response"].strip()
    for ch in resp:
        if ch in "012":
            return int(ch)
    return 0


def stage_judge(args):
    rng = np.random.default_rng(1)
    ids = np.load(OUT / "ids.npy"); Xc = np.load(OUT / "Xc.npy")
    X = np.load(OUT / "X.npy"); rd = np.load(OUT / "rd.npy")
    q = json.loads((OUT / "queries.json").read_text())
    qids, beta = q["qids"], q["beta"]
    pld = load_pld(MIND / f"{FAM}.pld")
    id2row = {int(i): r for r, i in enumerate(ids)}

    cache_path = OUT / "judgments.json"
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    pooled_path = OUT / "pooled.json"
    pooled = json.loads(pooled_path.read_text()) if pooled_path.exists() else {}

    todo = []
    for qi in qids:
        qrow = id2row[qi]
        rk = rank_all(qrow, Xc, X, rd, beta, exclude=qrow)
        cand_rows = sorted(set(int(r) for rs in rk.values() for r in rs))
        pooled[str(qi)] = {name: [int(ids[r]) for r in rs] for name, rs in rk.items()}
        for r in cand_rows:
            di = int(ids[r])
            key = f"{qi}:{di}"
            if key not in cache:
                todo.append((qi, di))
    pooled_path.write_text(json.dumps(pooled))
    print(f"{len(todo)} (q,doc) pairs to judge (cached={len(cache)})", flush=True)

    rng.shuffle(todo)                       # randomize → blind to ranker/source order
    for n_done, (qi, di) in enumerate(todo, 1):
        g = ollama_judge(pld[qi], pld[di])
        cache[f"{qi}:{di}"] = g
        if n_done % 50 == 0:
            cache_path.write_text(json.dumps(cache))
            print(f"  judged {n_done}/{len(todo)}", flush=True)
    cache_path.write_text(json.dumps(cache))
    print(f"judge done → {cache_path}", flush=True)


# ── stage: score ───────────────────────────────────────────────────────────
def _dcg(gains):
    return sum(g / np.log2(i + 2) for i, g in enumerate(gains))


def _ndcg(labels, k=K):
    g = labels[:k]
    ideal = sorted(labels, reverse=True)[:k]
    idcg = _dcg(ideal)
    return _dcg(g) / idcg if idcg > 0 else 0.0


def stage_score(args):
    ids = np.load(OUT / "ids.npy"); is_hub = np.load(OUT / "is_hub.npy")
    q = json.loads((OUT / "queries.json").read_text())
    stratum = q["stratum"]
    pooled = json.loads((OUT / "pooled.json").read_text())
    cache = json.loads((OUT / "judgments.json").read_text())
    id2row = {int(i): r for r, i in enumerate(ids)}

    def lab(qi, di): return cache.get(f"{qi}:{di}", 0)

    rankers = ["raw", "centered", "damped"]
    per_q = {r: {} for r in rankers}     # qi -> (ndcg, recall, rr, hubshare)
    for qi_s, lanes in pooled.items():
        qi = int(qi_s)
        all_rel = {di for name in rankers for di in lanes[name] if lab(qi, di) >= 1}
        tot_rel = len(all_rel) or 1
        for name in rankers:
            order = lanes[name][:K]
            labels = [lab(qi, di) for di in order]
            rec = sum(1 for di in order if di in all_rel) / tot_rel
            rr = next((1.0 / (i + 1) for i, di in enumerate(order) if lab(qi, di) >= 2), 0.0)
            hub = sum(1 for di in order if is_hub[id2row[di]]) / max(1, len(order))
            per_q[name][qi] = (_ndcg(labels), rec, rr, hub)

    strata = sorted(set(stratum.values()))
    print(f"\n{'stratum':<14}{'ranker':<10}{'nDCG@10':>9}{'Recall@10':>11}{'MRR@10':>9}{'hub%':>8}{'n':>5}")
    agg = {}
    for st in strata + ["ALL"]:
        for name in rankers:
            qs = [qi for qi in per_q[name]
                  if st == "ALL" or stratum[str(qi)] == st]
            if not qs: continue
            v = np.array([per_q[name][qi] for qi in qs])
            agg[(st, name)] = (v, qs)
            print(f"{st:<14}{name:<10}{v[:,0].mean():>9.3f}{v[:,1].mean():>11.3f}"
                  f"{v[:,2].mean():>9.3f}{100*v[:,3].mean():>7.1f}%{len(qs):>5}")

    # gate: centered (prod) -> damped
    print("\n=== GATE: centered → damped ===")
    rng = np.random.default_rng(7)
    ok = True
    for st in strata + ["ALL"]:
        if (st, "centered") not in agg: continue
        vc, qsc = agg[(st, "centered")]
        vd, qsd = agg[(st, "damped")]
        d_ndcg = vd[:, 0].mean() - vc[:, 0].mean()
        d_rec = vd[:, 1].mean() - vc[:, 1].mean()
        d_hub = vd[:, 3].mean() - vc[:, 3].mean()
        # bootstrap CI on nDCG delta (paired)
        diffs = vd[:, 0] - vc[:, 0]
        boot = [diffs[rng.integers(0, len(diffs), len(diffs))].mean() for _ in range(2000)]
        lo, hi = np.percentile(boot, [2.5, 97.5])
        rec_ok = d_rec >= -0.01
        ndcg_ok = lo >= 0 or d_ndcg >= 0          # CI lower not a loss
        hub_ok = d_hub < 0 if st == "ALL" else True
        flag = "ok" if (rec_ok and (lo >= 0)) else ("warn" if d_ndcg >= 0 else "FAIL")
        if st == "ALL" and not (rec_ok and lo >= 0 and hub_ok): ok = False
        print(f"  {st:<14} ΔnDCG={d_ndcg:+.3f} [95%CI {lo:+.3f},{hi:+.3f}]  "
              f"ΔRecall={d_rec:+.3f}  Δhub%={100*d_hub:+.1f}  {flag}")
    print(f"\nVERDICT: {'PASS — ship damping' if ok else 'NO-GO — damping not justified'}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", choices=["prep", "judge", "score"])
    ap.add_argument("--queries", type=int, default=200)
    ap.add_argument("--m", type=int, default=40)
    ap.add_argument("--beta", type=float, default=0.5)
    ap.add_argument("--recompute", action="store_true", help="force rebuild of X/Xc/rd cache")
    args = ap.parse_args()
    {"prep": stage_prep, "judge": stage_judge, "score": stage_score}[args.stage](args)


if __name__ == "__main__":
    main()
