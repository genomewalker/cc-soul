#!/usr/bin/env python3
"""M0 — injection precision (roadmap 49d1d679, the never-taken measurement).

Read-only offline log analysis. For every turn where the soul hook injected a
memory block (transcript role=attachment, lines "[lane][score%] [kind] text"),
measure whether the reasoner actually USED each injected memory in its response
to that turn. Definition of "used" mirrors the live implicit-resonance detector
(stop-core.sh:525-540): a distinctive content span from the injected line
reappears in the assistant's reply. Explicit [USED:<numeric-id>] markers count
as a hit for that turn too (they don't name which line, so they only lift the
turn-level signal, never the per-line denominator).

Outputs: overall per-memory precision, per-lane precision, tokens-per-used-
memory, turn-level hit-rate. No store, no daemon, no GPU.

Why content-overlap not id-join: injected lines carry NO memory id (prompt-
core.sh:360 hashes content precisely because ids are usually absent), so a
by-id join is impossible for most injections. Content reuse is the same signal
the production ack path already trusts.
"""

import argparse
import glob
import hashlib
import json
import re
from collections import defaultdict

INJ_LINE = re.compile(r"^\[(sem|hyb|kw|corr|xr)\]\[(\d+)%\]\s*(?:\[[^\]]+\]\s*)*(.*)$")
USED_REAL = re.compile(r"\[USED:(\d{3,})\]")  # numeric id only, not the literal "id" placeholder
STOP = set("the a an and or of to in is for with on at by be this that it as from".split())


def norm_tokens(s: str) -> set:
    return {w for w in re.findall(r"[a-z0-9][a-z0-9_>-]{3,}", s.lower()) if w not in STOP}


def texts(o):
    if isinstance(o, str):
        yield o
    elif isinstance(o, dict):
        for v in o.values():
            yield from texts(v)
    elif isinstance(o, list):
        for v in o:
            yield from texts(v)


def assistant_text(o) -> str:
    if o.get("type") != "assistant":
        return ""
    msg = o.get("message", o)
    parts = []
    for blk in msg.get("content", []) if isinstance(msg, dict) else []:
        if isinstance(blk, dict) and blk.get("type") == "text":
            parts.append(blk.get("text", ""))
    return "\n".join(parts)


TEMPLATE_PLACEHOLDER = "[type] text"  # prompt-core.sh:287 sed template, not a real injection


def injected_lines(o):
    """Return [(lane, score, content)] if this record is an injected soul block.

    Scans ALL text fields (not just the first) and dedups by (lane, content):
    an attachment can render sem in one field and hyb/kw/corr in another, so
    the old `if out: break` after the first matching field silently dropped the
    non-sem lanes — the parser gap that made M0 sem-only. Filters the literal
    `[type] text` template placeholder (an unfilled sed template, not a memory).
    """
    if o.get("type") not in ("attachment", "user"):
        return []
    out = []
    seen = set()
    for t in texts(o):
        if not any(tag in t for tag in ("[sem]", "[hyb]", "[kw]", "[corr]", "[xr]")):
            continue
        for ln in t.splitlines():
            s = ln.strip()
            m = INJ_LINE.match(s)
            if not m:
                continue
            lane, score, content = m.group(1), int(m.group(2)), m.group(3).strip()
            if not content or content == TEMPLATE_PLACEHOLDER:
                continue
            key = (lane, content)
            if key in seen:
                continue
            seen.add(key)
            # Reconstruct the hook's dedup/shadow hash: the hook hashes the
            # lane-marker-stripped line (["[NN%] [kind] content"])[:80]. Rendered
            # to the transcript as "[lane]" + that, so drop the leading "[lane]".
            hookline = s[len(lane) + 2:]
            h = hashlib.md5(hookline[:80].encode()).hexdigest()[:16]
            out.append((lane, score, content, h))
    return out


def is_self_referential(path: str) -> bool:
    # sessions that EDIT the hooks contain [USED:id]/[sem] as literal source code
    return "cc-soul" in path or path.endswith("-home-kbd606.jsonl")


def used(inj_content: str, reply_tokens: set, min_overlap: float) -> bool:
    it = norm_tokens(inj_content)
    if len(it) < 3:
        return False
    return len(it & reply_tokens) / len(it) >= min_overlap


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glob", default="/home/kbd606/.claude/projects/*/*.jsonl")
    ap.add_argument("--min-overlap", type=float, default=0.40)
    ap.add_argument("--max-sessions", type=int, default=0)
    args = ap.parse_args()

    inj_total = 0
    inj_used = 0
    lane_total = defaultdict(int)
    lane_used = defaultdict(int)
    turns = 0
    turns_hit = 0
    used_chars = 0
    inj_chars = 0
    sessions = 0
    skipped_self = 0

    files = sorted(glob.glob(args.glob))
    for path in files:
        if is_self_referential(path):
            skipped_self += 1
            continue
        if args.max_sessions and sessions >= args.max_sessions:
            break
        try:
            records = [json.loads(l) for l in open(path) if l.strip()]
        except Exception:
            continue
        pending = None  # injected block awaiting the next assistant reply
        saw = False
        for o in records:
            inj = injected_lines(o)
            if inj:
                pending = inj
                continue
            reply = assistant_text(o)
            if reply and pending:
                saw = True
                rt = norm_tokens(reply)
                explicit = bool(USED_REAL.search(reply))
                turns += 1
                hit_this_turn = explicit
                for lane, _score, content, _h in pending:
                    inj_total += 1
                    lane_total[lane] += 1
                    inj_chars += len(content)
                    u = used(content, rt, args.min_overlap) or explicit
                    if u:
                        inj_used += 1
                        lane_used[lane] += 1
                        used_chars += len(content)
                        hit_this_turn = True
                if hit_this_turn:
                    turns_hit += 1
                pending = None
        if saw:
            sessions += 1

    print(f"sessions_analyzed={sessions} skipped_self_referential={skipped_self}")
    print(f"injected_memories={inj_total} used={inj_used}")
    if inj_total:
        print(f"PER-MEMORY PRECISION = {inj_used/inj_total:.3f}")
    if turns:
        print(f"turn_level_hit_rate = {turns_hit/turns:.3f}  ({turns_hit}/{turns} turns used >=1 injected memory)")
    if inj_used:
        print(f"tokens_per_used_memory ~= {inj_chars/inj_used:.0f} injected_chars / used_memory (chars, /4 ~= tokens)")
    print("PER-LANE:")
    for lane in ("sem", "hyb", "kw", "corr", "xr"):
        t = lane_total[lane]
        if t:
            print(f"  {lane:4s} n={t:6d} used={lane_used[lane]:6d} precision={lane_used[lane]/t:.3f}")


if __name__ == "__main__":
    main()
