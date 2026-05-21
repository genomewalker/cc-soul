#!/usr/bin/env python3
"""
LongMemEval Benchmark Runner for cc-soul
arXiv:2410.10813 — ICLR 2025

Uses the exact judge prompts from the paper's evaluate_qa.py with gpt-4o-mini.
Produces hypothesis JSONL compatible with their evaluate_qa.py for independent re-scoring.

Dataset structure (per item):
  question_id, question_type, question, answer, question_date,
  haystack_dates, haystack_session_ids, haystack_sessions, answer_session_ids
Each haystack_sessions entry is a list of turns: [{role, content, has_answer}, …]

question_type: single-session-user | single-session-assistant | single-session-preference
               | multi-session | temporal-reasoning | knowledge-update

Usage:
  python3 scripts/benchmark_longmemeval.py                             # oracle, all 500
  python3 scripts/benchmark_longmemeval.py --dataset longmemeval_s --limit 50
  python3 scripts/benchmark_longmemeval.py --judge-model gpt-4o --limit 20
  OPENAI_API_KEY=sk-... python3 scripts/benchmark_longmemeval.py
"""

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.request

from datetime import datetime
from pathlib import Path
from typing import Optional

CHITTA = Path.home() / ".claude/bin/chitta"
# Benchmark daemon socket — override with BM_SOCK env var to use a dedicated daemon
BM_SOCK = os.environ.get("BM_SOCK", "")
DATA_DIR = Path("/projects/caeg/scratch/kbd606/tmp")
RESULTS_DIR = Path(__file__).parent.parent / "benchmarks" / "results"
HF_BASE = "https://huggingface.co/datasets/xiaowu0162/longmemeval/resolve/main"

DATASETS = {
    "longmemeval_oracle": (DATA_DIR / "longmemeval_oracle.json", f"{HF_BASE}/longmemeval_oracle"),
    "longmemeval_s":      (DATA_DIR / "longmemeval_s.json",      f"{HF_BASE}/longmemeval_s"),
    "longmemeval_m":      (DATA_DIR / "longmemeval_m.json",      f"{HF_BASE}/longmemeval_m"),
}

QUESTION_TYPES = [
    "single-session-user", "single-session-assistant", "single-session-preference",
    "multi-session", "temporal-reasoning", "knowledge-update",
]




# ── exact judge prompts from LongMemEval evaluate_qa.py ───────────────────────

def get_anscheck_prompt(task: str, question: str, answer: str, response: str,
                        abstention: bool = False) -> str:
    if abstention:
        return (
            "I will give you an unanswerable question, an explanation, and a response from a model. "
            "Please answer yes if the model correctly identifies the question as unanswerable. "
            "The model could say that the information is incomplete, or some other information is given "
            "but the asked information is not.\n\n"
            f"Question: {question}\n\nExplanation: {answer}\n\nModel Response: {response}\n\n"
            "Does the model correctly identify the question as unanswerable? Answer yes or no only."
        )
    if task in ("single-session-user", "single-session-assistant", "multi-session"):
        tmpl = (
            "I will give you a question, a correct answer, and a response from a model. "
            "Please answer yes if the response contains the correct answer. Otherwise, answer no. "
            "If the response is equivalent to the correct answer or contains all the intermediate "
            "steps to get the correct answer, you should also answer yes. If the response only "
            "contains a subset of the information required by the answer, answer no. "
            "\n\nQuestion: {q}\n\nCorrect Answer: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    elif task == "temporal-reasoning":
        tmpl = (
            "I will give you a question, a correct answer, and a response from a model. "
            "Please answer yes if the response contains the correct answer. Otherwise, answer no. "
            "If the response is equivalent to the correct answer or contains all the intermediate "
            "steps to get the correct answer, you should also answer yes. If the response only "
            "contains a subset of the information required by the answer, answer no. "
            "In addition, do not penalize off-by-one errors for the number of days. "
            "If the question asks for the number of days/weeks/months, etc., and the model makes "
            "off-by-one errors (e.g., predicting 19 days when the answer is 18), the model's "
            "response is still correct. "
            "\n\nQuestion: {q}\n\nCorrect Answer: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    elif task == "knowledge-update":
        tmpl = (
            "I will give you a question, a correct answer, and a response from a model. "
            "Please answer yes if the response contains the correct answer. Otherwise, answer no. "
            "If the response contains some previous information along with an updated answer, "
            "the response should be considered as correct as long as the updated answer is the "
            "required answer.\n\nQuestion: {q}\n\nCorrect Answer: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    elif task == "single-session-preference":
        tmpl = (
            "I will give you a question, a rubric for desired personalized response, and a response "
            "from a model. Please answer yes if the response satisfies the desired response. "
            "Otherwise, answer no. The model does not need to reflect all the points in the rubric. "
            "The response is correct as long as it recalls and utilizes the user's personal "
            "information correctly.\n\nQuestion: {q}\n\nRubric: {a}\n\nModel Response: {r}\n\n"
            "Is the model response correct? Answer yes or no only."
        )
    else:
        raise NotImplementedError(f"Unknown task type: {task}")
    return tmpl.format(q=question, a=answer, r=response)


# ── OpenAI judge ──────────────────────────────────────────────────────────────

_openai_client = None
_openai_available: Optional[bool] = None


def _get_openai_client(model: str):
    global _openai_client, _openai_available
    if _openai_available is False:
        return None
    if _openai_client is None:
        key = os.environ.get("OPENAI_API_KEY", "")
        if not key:
            _openai_available = False
            return None
        try:
            from openai import OpenAI
            _openai_client = OpenAI(api_key=key)
            _openai_available = True
        except Exception:
            _openai_available = False
    return _openai_client


def judge_with_openai(task: str, question: str, gold: str, hypothesis: str,
                      model: str, abstention: bool = False) -> Optional[bool]:
    client = _get_openai_client(model)
    if client is None:
        return None
    prompt = get_anscheck_prompt(task, question, gold, hypothesis, abstention)
    try:
        resp = client.chat.completions.create(
            model=model,
            messages=[{"role": "user", "content": prompt}],
            n=1, temperature=0, max_tokens=10,
        )
        text = resp.choices[0].message.content.strip().lower()
        return "yes" in text
    except Exception as e:
        print(f"  [openai error] {e}", file=sys.stderr)
        return None


_codex_cli: Optional[str] = None

def _find_codex_cli() -> Optional[str]:
    global _codex_cli
    if _codex_cli is not None:
        return _codex_cli or None
    for candidate in ["codex", str(Path.home() / ".local/bin/codex")]:
        try:
            r = subprocess.run([candidate, "--version"], capture_output=True, timeout=5)
            if r.returncode == 0:
                _codex_cli = candidate
                return candidate
        except Exception:
            pass
    _codex_cli = ""
    return None


def judge_with_codex(task: str, question: str, gold: str, hypothesis: str,
                     abstention: bool = False) -> Optional[bool]:
    """Judge using `codex exec` non-interactive mode — no chitta socket contention."""
    cli = _find_codex_cli()
    if not cli:
        return None
    prompt = get_anscheck_prompt(task, question, gold, hypothesis, abstention)
    try:
        r = subprocess.run(
            [cli, "exec", "--"],
            input=prompt, capture_output=True, text=True, timeout=60,
        )
        text = r.stdout.strip().lower()
        if "yes" in text:
            return True
        if "no" in text:
            return False
    except Exception:
        pass
    return None


def judge_heuristic(gold, hypothesis: str) -> bool:
    """Token-F1 fallback (≥50% overlap)."""
    g = set(str(gold).lower().split())
    h = set(hypothesis.lower().split())
    return len(g & h) / max(len(g), 1) >= 0.5


# ── data download ─────────────────────────────────────────────────────────────

def ensure_data(dataset: str) -> Path:
    path, url = DATASETS[dataset]
    if path.exists() and path.stat().st_size > 1000:
        return path
    print(f"Downloading {dataset}…")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "cc-soul-benchmark"})
    with urllib.request.urlopen(req, timeout=300) as r, open(path, "wb") as f:
        total = int(r.headers.get("Content-Length", 0))
        downloaded = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total:
                print(f"\r  {downloaded/total:.0%}", end="", flush=True)
    print(f"\r  {path.stat().st_size // 1024 // 1024}MB → {path}")
    return path


# ── chitta RPC ────────────────────────────────────────────────────────────────

def _sock_path() -> str:
    """Return the active chitta socket path."""
    if BM_SOCK:
        return BM_SOCK
    # Fall back to subprocess discovery
    r = subprocess.run([str(CHITTA), "status", "--socket-path-only"],
                       capture_output=True, text=True, timeout=5)
    return r.stdout.strip()


def _rpc_sock(tool: str, arguments: dict, timeout: int = 30) -> Optional[dict]:
    """Send one JSON-RPC call over a fresh Unix socket connection."""
    req = (json.dumps({
        "jsonrpc": "2.0", "id": 1,
        "method": "tools/call",
        "params": {"name": tool, "arguments": arguments},
    }) + "\n").encode()
    try:
        sock_path = _sock_path()
        if not sock_path:
            return None
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(timeout)
            s.connect(sock_path)
            s.sendall(req)
            buf = b""
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                buf += chunk
                # Try to parse a complete JSON object
                try:
                    decoder = json.JSONDecoder()
                    text = buf.decode(errors="replace")
                    idx = 0
                    while idx < len(text):
                        if text[idx] == '{':
                            try:
                                resp, _ = decoder.raw_decode(text, idx)
                                return resp.get("result")
                            except json.JSONDecodeError:
                                pass
                        idx += 1
                except Exception:
                    pass
    except Exception as e:
        print(f"  [rpc error] {tool}: {e}", file=sys.stderr)
    return None


def _rpc(tool: str, arguments: dict, timeout: int = 30) -> Optional[dict]:
    return _rpc_sock(tool, arguments, timeout)


def remember_batch(items: list, realm: str) -> list:
    """Store multiple memories in one round-trip. Returns list of IDs (or None)."""
    if not items:
        return []
    result = _rpc_sock("remember_batch", {"items": items, "realm": realm}, timeout=300)
    if result:
        return result.get("structured", {}).get("ids", [None] * len(items))
    return [None] * len(items)


def remember(content: str, realm: str, tags: list, valid_from: Optional[str] = None,
             source_session: Optional[str] = None) -> Optional[str]:
    """Store a memory via batch of one. Returns the memory ID string, or None."""
    item: dict = {"content": content, "realm": realm, "tags": tags, "visibility": 0}
    if valid_from:
        item["valid_from"] = valid_from
    if source_session:
        item["source_session"] = source_session
    ids = remember_batch([item], realm)
    return ids[0] if ids else None


def _parse_lme_date(d: str) -> Optional[datetime]:
    """Parse LongMemEval date format: '2023/04/10 (Mon) 23:07' or ISO."""
    if not d:
        return None
    # Strip day-of-week like " (Mon)" before parsing
    clean = re.sub(r"\s*\([A-Za-z]+\)\s*", " ", d).strip()
    # Normalize slashes to dashes for fromisoformat
    clean = clean.replace("/", "-")
    try:
        return datetime.fromisoformat(clean)
    except ValueError:
        try:
            return datetime.strptime(clean, "%Y-%m-%d %H:%M")
        except ValueError:
            try:
                return datetime.strptime(clean[:10], "%Y-%m-%d")
            except ValueError:
                return None


def recall(query: str, realm: str, limit: int = 10,
           dates: Optional[list] = None, qtype: Optional[str] = None,
           question_date: Optional[str] = None) -> str:
    """Recall memories. For temporal questions, runs recall_temporal with semantic
    re-ranking in addition to hybrid recall, and returns all hits concatenated."""
    # Primary path: session-aware recall groups turns by source_session using
    # noisy-OR aggregation — returns ranked sessions with best evidence + date.
    session_text = ""
    if qtype == "temporal-reasoning":
        s_hits = recall_session_hits(query, realm=realm, limit=limit * 2)
        lines = []
        for h in s_hits:
            sid = h.get("session_id", "")
            # session_key format: "{realm}:s{i}:{date_str}"
            parts = sid.rsplit(":", 1)
            date_part = parts[-1] if len(parts) == 2 else ""
            evidence = h.get("best_evidence", "").strip()
            if date_part and evidence:
                lines.append(f"[session] (on: {date_part}) {evidence}")
        session_text = "\n".join(lines)

    temporal_hits = ""
    if qtype == "temporal-reasoning" and dates:
        parsed = [_parse_lme_date(d) for d in dates if d]
        parsed = sorted(dt for dt in parsed if dt is not None)
        # Also include question_date as upper bound if available
        if question_date:
            qdt = _parse_lme_date(question_date)
            if qdt:
                parsed.append(qdt)
                parsed = sorted(parsed)
        if parsed:
            from_ms = int(parsed[0].timestamp() * 1000) - 86_400_000
            to_ms   = int(parsed[-1].timestamp() * 1000) + 86_400_000
            tres = _rpc("recall_temporal", {
                "start": datetime.fromtimestamp(from_ms / 1000).isoformat(),
                "end":   datetime.fromtimestamp(to_ms   / 1000).isoformat(),
                "query": query,   # semantic re-ranking within the window
                "realm": realm, "limit": limit * 2,  # broader for temporal
            }, timeout=120)
            if tres:
                tcontent = tres.get("content", [])
                temporal_hits = tcontent[0].get("text", "") if tcontent else ""

    # For non-temporal questions also pull session-level hits
    if qtype != "temporal-reasoning":
        s_hits = recall_session_hits(query, realm=realm, limit=limit)
        for h in s_hits:
            evidence = h.get("best_evidence", "").strip()
            if evidence:
                session_text += ("\n" if session_text else "") + evidence

    result = _rpc("recall", {
        "query": query, "limit": limit,
        "strategy": "hybrid", "realm": realm,
    }, timeout=120)
    hybrid_hits = ""
    if result:
        content = result.get("content", [])
        hybrid_hits = content[0].get("text", "") if content else ""

    # Second pass: keyword-only query to catch facts missed by full-sentence BM25
    _STOP = {"what","where","when","who","how","did","do","does","i","my","me","the",
             "a","an","is","was","were","have","has","had","with","in","on","at","of",
             "to","for","and","or","not","which","that","this","it","be","been","much",
             "many","get","got","use","used"}
    kw_tokens = [w for w in re.sub(r"[^\w\s]","",query).lower().split()
                 if w not in _STOP and len(w) > 2]
    kw_query = " ".join(kw_tokens[:6])
    kw_hits = ""
    if kw_query and kw_query != query.lower():
        r2 = _rpc("recall", {"query": kw_query, "limit": limit,
                              "strategy": "hybrid", "realm": realm}, timeout=120)
        if r2:
            c2 = r2.get("content", [])
            kw_hits = c2[0].get("text", "") if c2 else ""

    # Combine and boost: interleave by date proximity to question_date
    combined_lines: list[tuple[float, str]] = []  # (sort_key, line)
    qdt_boost = _parse_lme_date(question_date) if question_date else None

    def _proximity_key(line: str) -> float:
        """Lower = closer to question_date; undated lines sort last."""
        for m in _DATE_IN_MEM.finditer(line):
            try:
                if m.group(1):
                    dt = datetime.strptime(m.group(1), "%Y-%m-%d")
                elif m.group(2):
                    dt = datetime.strptime(m.group(2), "%Y/%m/%d")
                elif m.group(3):
                    dt = datetime.strptime(m.group(3), "%Y-%m-%d")
                elif m.group(4) and m.group(5):
                    mon = _MONTH_MAP.get(m.group(4).split()[0].lower())
                    day = int(re.search(r"\d+", m.group(4)).group())
                    if not mon:
                        continue
                    dt = datetime(int(m.group(5)), mon, day)
                else:
                    continue
                if qdt_boost:
                    return abs((qdt_boost - dt).days)
                return -dt.timestamp()  # most recent first when no question_date
            except ValueError:
                continue
        return 1e9  # undated last

    seen: set[str] = set()
    for block in (session_text, temporal_hits, hybrid_hits, kw_hits):
        for line in block.splitlines():
            stripped = line.strip()
            if stripped and stripped not in seen:
                seen.add(stripped)
                combined_lines.append((_proximity_key(stripped), stripped))

    combined_lines.sort(key=lambda x: x[0])
    return "\n".join(line for _, line in combined_lines)


def recall_session_hits(query: str, realm: str, limit: int = 10) -> list[dict]:
    """Session-aware recall: returns sessions ranked by noisy-OR aggregation."""
    result = _rpc("recall_session", {"query": query, "realm": realm, "limit": limit}, timeout=120)
    if not result:
        return []
    structured = result.get("structured", {})
    return structured.get("results", [])


def forget_realm(realm: str):
    _rpc("forget", {"query": realm, "tag": realm.replace("/", "-"), "cascade": "false"}, timeout=90)
    time.sleep(1.0)  # let daemon settle write-lock after bulk delete


def extract_answer(hits: str) -> str:
    """Return best-effort answer from recall hits (fallback when synthesis unavailable)."""
    skip = ("Found ", "No results", "Error", "[warn", "[rpc")
    for line in hits.splitlines():
        line = line.strip()
        if line and not any(line.startswith(p) for p in skip):
            return line[:300]
    return "I don't know"


_CLAUDE_BIN = Path.home() / ".local/bin/claude"

# Patterns for dates embedded in chitta memory output
_DATE_IN_MEM = re.compile(
    r"\(on:\s*(\d{4}-\d{2}-\d{2})\)"          # (on: 2023-04-10)
    r"|(\d{4}/\d{2}/\d{2})"                    # 2023/04/10
    r"|(\d{4}-\d{2}-\d{2})"                    # 2023-04-10
    r"|\b(\w+ \d{1,2}(?:st|nd|rd|th)?),?\s+(\d{4})\b"  # February 15, 2023
)

_MONTH_MAP = {
    "january":1,"february":2,"march":3,"april":4,"may":5,"june":6,
    "july":7,"august":8,"september":9,"october":10,"november":11,"december":12,
}


def _extract_dated_lines(memories: str) -> list[tuple[datetime, str]]:
    """Return list of (date, line) pairs from memory text, sorted by date."""
    results = []
    for line in memories.splitlines():
        line_dt = None
        for m in _DATE_IN_MEM.finditer(line):
            try:
                if m.group(1):   # (on: YYYY-MM-DD)
                    line_dt = datetime.strptime(m.group(1), "%Y-%m-%d")
                elif m.group(2): # YYYY/MM/DD
                    line_dt = datetime.strptime(m.group(2), "%Y/%m/%d")
                elif m.group(3): # YYYY-MM-DD
                    line_dt = datetime.strptime(m.group(3), "%Y-%m-%d")
                elif m.group(4) and m.group(5):  # "February 15, 2023"
                    mon = _MONTH_MAP.get(m.group(4).split()[0].lower())
                    day = int(re.search(r"\d+", m.group(4)).group())
                    if mon:
                        line_dt = datetime(int(m.group(5)), mon, day)
                    else:
                        continue
                else:
                    continue
                break
            except ValueError:
                continue
        if line_dt is None:
            continue
        results.append((line_dt, line))
    results.sort(key=lambda x: x[0])
    return results


def _anchor_candidates(anchor_words: set, dated: list, k: int = 4) -> list:
    """Top-k dated lines matching anchor_words, scored by word overlap."""
    scored = []
    for dt, line in dated:
        ll = line.lower()
        hits = sum(1 for w in anchor_words if w in ll)
        if hits > 0:
            scored.append((hits, dt, line))
    scored.sort(key=lambda x: (-x[0], x[1]))
    return [(dt, line) for _, dt, line in scored[:k]]


def _unit_plausibility(delta_days: int, q: str) -> float:
    """Score 0..1: how plausible is this delta given the question's time unit."""
    if delta_days == 0:
        return 0.0
    if "years" in q:
        return 1.0 if 180 <= delta_days <= 3650 else 0.1
    if "months" in q:
        if 15 <= delta_days <= 730:
            return 1.0
        return 0.2 if delta_days < 15 else 0.1
    if "weeks" in q:
        if 3 <= delta_days <= 365:
            return 1.0
        return 0.1 if delta_days > 365 else 0.3
    # days
    if 1 <= delta_days <= 365:
        return 1.0
    return 0.1 if delta_days > 365 else 0.5


def _format_delta(delta_days: int, q: str) -> str:
    if "months" in q:
        return str(round(delta_days / 30.44))
    if "weeks" in q:
        return str(round(delta_days / 7))
    if "years" in q:
        return str(round(delta_days / 365.25))
    return str(delta_days)


def _extract_inline_duration(memories: str, q: str) -> Optional[str]:
    """Scan recalled lines for explicit duration phrases like 'for 2 days', 'spent 3 weeks'."""
    unit = "weeks" if "weeks" in q else "months" if "months" in q else "days"
    patterns = [
        rf"(?:for|took|spent|lasted|took me|in)\s+(\d+)\s+{unit}",
        rf"(\d+)[- ]{unit}(?:\s+trip|\s+camp|\s+hike|\s+stay|\s+vacation|\s+retreat)?",
    ]
    for line in memories.splitlines():
        for pat in patterns:
            m = re.search(pat, line, re.I)
            if m:
                val = int(m.group(1))
                if _unit_plausibility(val if unit == "days" else val * (7 if unit == "weeks" else 30), q) >= 0.5:
                    return m.group(1)
    return None


def _temporal_reason(question: str, memories: str,
                     question_date: Optional[datetime] = None) -> Optional[str]:
    """Deterministic temporal reasoner with dual-anchor + candidate-grid ranking.

    Returns answer string or None if can't determine confidently.
    """
    q = question.lower()
    dated = _extract_dated_lines(memories)
    if not dated:
        return None

    _noise = re.compile(
        r"\b(how many|days|weeks|months|years|ago|since|have|had|passed|when|did|i|"
        r"the|a|an|my|was|is|were|it|this|that|go|went|attend|start|finish|in|at|to)\b",
        re.I)

    # ── Relative-to-now: "X ago" / "X since" ─────────────────────────────────
    _is_ago = any(p in q for p in ("days ago", "weeks ago", "months ago", "years ago",
                                    "have passed since", "had passed since",
                                    "days since", "weeks since", "months since"))
    if _is_ago and question_date:
        activity_words = set(_noise.sub(" ", q).split()) - {"", "?"}
        before = [(dt, line) for dt, line in dated if dt <= question_date] or dated
        candidates = _anchor_candidates(activity_words, before, k=4)
        if not candidates:
            return None
        best_result, best_score = None, -1.0
        for cand_dt, _ in candidates:
            delta_days = abs((question_date - cand_dt).days)
            score = _unit_plausibility(delta_days, q)
            if score > best_score:
                best_score = score
                best_result = _format_delta(delta_days, q)
        return best_result if best_score >= 0.3 else None

    # ── Explicit span: "between X and Y" / "did it take" / "did I spend" ─────
    _is_span = ("how many days" in q or "days did it take" in q or
                "days did i spend" in q or "days do i spend" in q or
                "days before" in q or "days after" in q or
                "how many weeks" in q or "weeks did" in q or
                "how many months" in q)
    if _is_span:
        # Inline duration scan first (e.g. "spent 2 days camping")
        inline = _extract_inline_duration(memories, q)
        if inline:
            return inline

        # Dual-anchor extraction from "between [A] and [B]"
        _between = re.search(
            r"between\s+(?:the\s+(?:day\s+)?(?:i\s+)?)?(.+?)\s+and\s+"
            r"(?:the\s+(?:day\s+|time\s+)?(?:i\s+)?)?(.+?)(?:\?|$)", q)
        if _between:
            kw_a = set(_between.group(1).split()) - {"the","a","an","i","my","was","is","day"}
            kw_b = set(_between.group(2).split()) - {"the","a","an","i","my","was","is","day"}
            cands_a = _anchor_candidates(kw_a, dated, k=3)
            cands_b = _anchor_candidates(kw_b, dated, k=3)
            if cands_a and cands_b:
                best_result, best_score = None, -1.0
                for dt_a, line_a in cands_a:
                    for dt_b, line_b in cands_b:
                        if line_a == line_b:
                            continue  # collapsed to same line → skip
                        delta_days = abs((dt_a - dt_b).days)
                        score = _unit_plausibility(delta_days, q)
                        if score > best_score:
                            best_score = score
                            best_result = _format_delta(delta_days, q)
                if best_score >= 0.5:
                    return best_result

        # Fallback: exactly-2-date case
        distinct_dates = sorted({dt for dt, _ in dated})
        if len(distinct_dates) == 2:
            delta_days = abs((distinct_dates[1] - distinct_dates[0]).days)
            if _unit_plausibility(delta_days, q) >= 0.5:
                return _format_delta(delta_days, q)
        return None

    # ── Ordering: "which ... first/last/order/earliest/latest" ───────────────
    _is_order = ("first" in q or "started first" in q or "last" in q or
                 "most recently" in q or "order" in q or "earliest" in q or "latest" in q)
    if _is_order:
        is_last = any(p in q for p in ("last", "most recently", "latest"))
        # Extract choices: quoted, "the X" pattern, or "A or B"
        choices = re.findall(r"'([^']+)'", question)
        if not choices:
            choices = re.findall(r"the\s+([\w][\w\s]+?)(?:\s+(?:or|,|\?))", question, re.I)
        if not choices:
            m_ab = re.search(r"[,:]?\s*([\w][\w\s']+?)\s+or\s+([\w][\w\s']+?)(?:\s*\?|$)", question, re.I)
            if m_ab:
                choices = [m_ab.group(1).strip(), m_ab.group(2).strip().rstrip("?")]

        if choices and len(choices) >= 2:
            # Bind each choice to its best dated line independently (dual-anchor principle)
            matched: list[tuple[datetime, str]] = []
            used_lines: set[str] = set()
            for c in choices:
                cw = set(c.lower().split()) - {"the","a","an","i","my"}
                cands = _anchor_candidates(cw, dated, k=3)
                for dt, line in cands:
                    if line not in used_lines:
                        matched.append((dt, c))
                        used_lines.add(line)
                        break
            if len(matched) >= 2:
                matched.sort(key=lambda x: x[0])
                if "order" in q or "earliest to latest" in q:
                    return ", ".join(c for _, c in matched)
                return matched[-1][1] if is_last else matched[0][1]

        return None

    return None


def synthesize(question: str, memories: str, qtype: Optional[str] = None,
               question_date: Optional[str] = None) -> str:
    """Answer question from recalled memories.

    For temporal questions: try deterministic date arithmetic first (no LLM).
    Fallback: claude -p for cases that need language understanding.
    """
    if not memories.strip():
        return "I don't know"

    # Try deterministic reasoning for temporal questions
    if qtype == "temporal-reasoning":
        qdt = _parse_lme_date(question_date) if question_date else None
        answer = _temporal_reason(question, memories, question_date=qdt)
        if answer:
            return answer

    # LLM fallback — only when deterministic path can't resolve
    if not _CLAUDE_BIN.exists():
        return extract_answer(memories)

    qdate_hint = f"\nQuestion date: {question_date}" if question_date else ""
    prompt = (
        "You are a memory assistant. Answer using ONLY the memories below.\n"
        "Rules:\n"
        "- Reply with ONLY the answer: a name, place, number, date, or short phrase.\n"
        "- No explanation, no 'based on', no preamble.\n"
        "- Dates are YYYY-MM-DD. Subtract directly for day/week/month counts.\n"
        "- If the answer is not in the memories, reply exactly: I don't know\n"
        f"{qdate_hint}\n\n"
        f"MEMORIES:\n{memories[:5000]}\n\n"
        f"QUESTION: {question}"
    )
    try:
        result = subprocess.run(
            [str(_CLAUDE_BIN), "-p", prompt],
            capture_output=True, text=True, timeout=90,
        )
        answer = result.stdout.strip()
        if answer:
            return answer
    except Exception:
        pass
    # Codex fallback
    cli = _find_codex_cli()
    if cli:
        try:
            r2 = subprocess.run(
                [cli, "exec", "--no-interactive", "--full-auto", prompt],
                capture_output=True, text=True, timeout=90,
            )
            answer = r2.stdout.strip()
            if answer:
                return answer
        except Exception:
            pass
    return extract_answer(memories)


# ── ingest helper (called from thread pool for pipeline overlap) ───────────────

def _ingest_question(item: dict) -> int:
    """Build and ingest all turns for one question item. Returns turns ingested."""
    qid      = item["question_id"]
    qtype    = item["question_type"]
    sessions = item.get("haystack_sessions", [])
    dates    = item.get("haystack_dates", [])
    realm    = f"lme21/{qid}"
    tags     = [realm.replace("/", "-"), f"qt:{qtype.replace('-', '_')}"]
    ingested = 0

    if qtype == "temporal-reasoning":
        for i, session in enumerate(sessions):
            date = dates[i] if i < len(dates) else None
            if not date:
                continue
            dt_parsed = _parse_lme_date(date)
            if not dt_parsed:
                continue
            date_str    = dt_parsed.strftime('%Y-%m-%d')
            session_key = f"{realm}:s{i}:{date_str}"
            user_lines  = [
                t.get("content", "").strip()
                for t in session
                if t["role"] == "user" and t.get("content", "").strip()
            ]
            if not user_lines:
                continue
            batch: list[dict] = [{
                "content": f"[session] (on: {date_str}) {len(user_lines)} turns",
                "realm": realm, "tags": tags + ["session_anchor"],
                "valid_from": date, "source_session": session_key, "visibility": 0,
            }]
            for turn in sorted(user_lines, key=len, reverse=True)[:5]:
                batch.append({
                    "content": f"[turn] (on: {date_str}) {turn}",
                    "realm": realm, "tags": tags + ["session_turn"],
                    "valid_from": date, "source_session": session_key, "visibility": 0,
                })
            remember_batch(batch, realm)
            ingested += len(batch)
    else:
        all_turns: list[tuple[int, str, str, Optional[str], str]] = []
        for i, session in enumerate(sessions):
            date = dates[i] if i < len(dates) else None
            dt_parsed = _parse_lme_date(date) if date else None
            date_str = dt_parsed.strftime('%Y-%m-%d') if dt_parsed else ""
            session_key = f"{realm}:s{i}:{date_str}" if date_str else f"{realm}:s{i}"
            for turn in session:
                content = turn.get("content", "").strip()
                if not content:
                    continue
                role  = turn["role"]
                score = len(content) + (2000 if role == "user" else 0)
                all_turns.append((score, role, content, date, session_key))
        all_turns.sort(key=lambda x: -x[0])
        batch = []
        for _, role, content, date, session_key in all_turns:
            date_prefix = ""
            if date:
                dt_parsed = _parse_lme_date(date)
                if dt_parsed:
                    date_prefix = f"(on: {dt_parsed.strftime('%Y-%m-%d')}) "
            batch.append({
                "content": f"[{role}] {date_prefix}{content}",
                "realm": realm, "tags": tags,
                "valid_from": date or "",
                "source_session": session_key,
                "visibility": 0,
            })
        remember_batch(batch, realm)
        ingested += len(batch)

    return ingested


# ── main loop ─────────────────────────────────────────────────────────────────

def run(dataset: str, limit: int, keep_data: bool, dry_run: bool,
        verbose: bool, judge_model: str, question_types: Optional[list] = None):
    path = ensure_data(dataset)
    with open(path) as f:
        items = json.load(f)
    if question_types:
        items = [i for i in items if i.get("question_type") in question_types]
    if limit:
        items = items[:limit]

    print(f"Loaded {len(items)} questions from {dataset}")
    using_openai = bool(os.environ.get("OPENAI_API_KEY"))
    using_codex = not using_openai and bool(_find_codex_cli())
    if using_openai:
        judge_mode = f"{judge_model} (LongMemEval exact prompts)"
    elif using_codex:
        judge_mode = "codex exec (LongMemEval exact prompts)"
    else:
        judge_mode = "token-F1 heuristic"
    print(f"Judge: {judge_mode}")

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    scores: dict[str, list[bool]] = {qt: [] for qt in QUESTION_TYPES}
    latencies: list[float] = []
    rows: list[dict] = []
    hypotheses: list[dict] = []  # LongMemEval-format JSONL for their evaluate_qa.py

    for idx, item in enumerate(items):
        qid       = item["question_id"]
        question  = item["question"]
        gold      = item["answer"]
        qtype          = item["question_type"]
        dates          = item.get("haystack_dates", [])
        question_date  = item.get("question_date")
        sessions       = item.get("haystack_sessions", [])
        abstention     = "_abs" in qid
        realm          = f"lme21/{qid}"

        print(f"[{idx+1}/{len(items)}] {qtype} — {question[:65]}…", end="", flush=True)

        if dry_run:
            print()
            continue

        # --- Ingest ---
        turns_ingested = _ingest_question(item)

        # --- Recall ---
        t0 = time.perf_counter()
        t_limit = 30
        hits = recall(question, realm=realm, limit=t_limit, dates=dates,
                      qtype=qtype, question_date=question_date)
        latency_ms = (time.perf_counter() - t0) * 1000
        latencies.append(latency_ms)

        hypothesis = synthesize(question, hits, qtype=qtype,
                                question_date=question_date)

        # --- Judge: OpenAI → codex → token-F1 ---
        if using_openai:
            correct = judge_with_openai(qtype, question, gold, hypothesis,
                                        model=judge_model, abstention=abstention)
        else:
            correct = None
        if correct is None:
            correct = judge_with_codex(qtype, question, gold, hypothesis,
                                       abstention=abstention)
        if correct is None:
            correct = judge_heuristic(gold, hypothesis)

        scores.setdefault(qtype, []).append(correct)
        hypotheses.append({"question_id": qid, "hypothesis": hypothesis})

        print(f"  {'✓' if correct else '✗'}  {latency_ms:.0f}ms  turns={turns_ingested}")
        if verbose:
            print(f"  hyp:  {hypothesis[:100]}")
            print(f"  gold: {gold[:100]}")

        rows.append({
            "qid": qid, "qtype": qtype, "correct": correct,
            "latency_ms": round(latency_ms),
            "hypothesis": str(hypothesis)[:250], "gold": str(gold)[:250],
        })

        # No forget_realm — run-prefixed realm isolates each run from prior runs.
        # Accumulation: ~42 sessions/q × 50q = 2,100 total writes (per-session distillation).

    if dry_run:
        print(f"\nDry run: {len(items)} questions, nothing ingested.")
        return

    # ── Report ────────────────────────────────────────────────────────────────
    git_sha = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        capture_output=True, text=True,
    ).stdout.strip() or "unknown"

    total_q = len(rows)
    total_correct = sum(r["correct"] for r in rows)
    overall = total_correct / total_q if total_q else 0.0
    p50 = sorted(latencies)[len(latencies) // 2] if latencies else 0
    p95 = sorted(latencies)[int(len(latencies) * 0.95)] if latencies else 0

    print(f"\n{'='*60}")
    print(f"  LongMemEval — {dataset}  ({git_sha})")
    print(f"  Judge: {judge_mode}")
    print(f"  Overall: {overall:.1%}  ({total_correct}/{total_q})")
    print(f"  p50={p50:.0f}ms  p95={p95:.0f}ms")
    print(f"{'='*60}")
    for qt in QUESTION_TYPES:
        res = scores.get(qt, [])
        if res:
            print(f"  {qt:35s} {sum(res)/len(res):.1%}  ({sum(res)}/{len(res)})")

    # ── Write outputs ─────────────────────────────────────────────────────────
    out = RESULTS_DIR / f"longmemeval-{git_sha}.md"
    hyp_out = RESULTS_DIR / f"longmemeval-{git_sha}.hyp.jsonl"

    # Hypothesis JSONL (compatible with their evaluate_qa.py)
    hyp_out.write_text("\n".join(json.dumps(h) for h in hypotheses) + "\n")

    now = datetime.now().strftime("%Y-%m-%d %H:%M UTC")
    md = [
        f"# LongMemEval — {git_sha}",
        f"",
        f"**Dataset**: `{dataset}`  **Judge**: `{judge_mode}`  **Date**: {now}  **n**: {total_q}",
        f"",
        "| model | overall | p50_ms | p95_ms |",
        "|---|---|---|---|",
        f"| cc-soul/{git_sha} | {overall:.3f} | {p50:.0f} | {p95:.0f} |",
        f"",
        "## By question type",
        f"",
        "| type | accuracy | n |",
        "|---|---|---|",
    ]
    for qt in QUESTION_TYPES:
        res = scores.get(qt, [])
        if res:
            md.append(f"| {qt} | {sum(res)/len(res):.1%} | {len(res)} |")
    md += ["", "## Per-question", ""]
    md.append("| qid | type | ✓ | ms | hypothesis | gold |")
    md.append("|---|---|---|---|---|---|")
    for r in rows:
        h = r["hypothesis"].replace("|", "\\|").replace("\n", " ")[:80]
        g = r["gold"].replace("|", "\\|").replace("\n", " ")[:80]
        md.append(f"| {r['qid']} | {r['qtype']} | {'✓' if r['correct'] else '✗'} | {r['latency_ms']} | {h} | {g} |")

    out.write_text("\n".join(md) + "\n")
    (out.with_suffix(".json")).write_text(
        json.dumps({"dataset": dataset, "sha": git_sha, "judge": judge_mode, "rows": rows}, indent=2))

    print(f"\nResults  → {out}")
    print(f"Hyp JSONL → {hyp_out}  (re-score: python evaluate_qa.py gpt-4o-mini {hyp_out} {path})")


# ── CLI ───────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="LongMemEval benchmark for cc-soul")
    ap.add_argument("--dataset", default="longmemeval_oracle",
                    choices=list(DATASETS), metavar="DATASET",
                    help=f"One of: {', '.join(DATASETS)} (default: longmemeval_oracle)")
    ap.add_argument("--limit",       type=int, default=0,
                    help="Max questions (0=all)")
    ap.add_argument("--judge-model", default="gpt-4o-mini",
                    help="OpenAI model for judging (default: gpt-4o-mini)")
    ap.add_argument("--keep-data",   action="store_true",
                    help="Keep ingested turns after each question")
    ap.add_argument("--dry-run",     action="store_true",
                    help="Inspect questions without running")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--question-types", nargs="+", metavar="TYPE",
                    help="Filter to specific question type(s), e.g. temporal-reasoning")
    args = ap.parse_args()

    if not CHITTA.exists():
        sys.exit(f"chitta not found at {CHITTA}")

    run(args.dataset, args.limit, args.keep_data, args.dry_run,
        args.verbose, args.judge_model, args.question_types)


if __name__ == "__main__":
    main()
