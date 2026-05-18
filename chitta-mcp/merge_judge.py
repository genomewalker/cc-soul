"""LLM-mediated write-merge judge.

Decides whether a new memory should be added, used to update an existing one,
or discarded as a duplicate. Uses Haiku for speed; results cached 60s by
content+candidate hash.
"""

import hashlib
import json
import time
from typing import Any

import anthropic as _ant

_client: _ant.Anthropic | None = None

def _get_client() -> _ant.Anthropic:
    global _client
    if _client is None:
        _client = _ant.Anthropic()
    return _client


_JUDGE_PROMPT = """\
You are a memory deduplication judge. Given a new memory and existing candidates, decide what to do.

Output valid JSON only — no prose, no markdown fences:
{"action": "add"|"update"|"discard", "target_id": <integer or null>, "reason": "<one sentence>"}

- "add": no sufficiently close match exists — store as new memory
- "update": a candidate covers the same fact but needs refreshing — supersede it with the new content
- "discard": an identical or stronger candidate already exists — skip storing

Be conservative: prefer "add" unless the overlap is clear and specific.

New memory:
{content}

Existing candidates (id, score, excerpt):
{candidates_text}
"""

_cache: dict[str, tuple[float, dict]] = {}
_CACHE_TTL = 60.0


def _cache_key(content: str, candidates: list[dict]) -> str:
    ids = sorted(str(c.get("id", "")) for c in candidates)
    raw = content[:256] + "|" + ",".join(ids)
    return hashlib.md5(raw.encode()).hexdigest()


def judge(content: str, candidates: list[dict[str, Any]]) -> dict:
    """Return {action, target_id, reason} for a new memory given recall candidates."""
    if not candidates:
        return {"action": "add", "target_id": None, "reason": "no candidates"}

    key = _cache_key(content, candidates)
    now = time.monotonic()
    if key in _cache and now - _cache[key][0] < _CACHE_TTL:
        return _cache[key][1]

    lines = []
    for c in candidates[:5]:
        cid = c.get("id", "?")
        score = c.get("score", 0.0)
        text = (c.get("content") or c.get("text") or "")[:120].replace("\n", " ")
        lines.append(f"  [{cid}] score={score:.3f} — {text}")
    candidates_text = "\n".join(lines) if lines else "  (none)"

    prompt = _JUDGE_PROMPT.format(content=content[:500], candidates_text=candidates_text)

    try:
        resp = _get_client().messages.create(
            model="claude-haiku-4-5-20251001",
            max_tokens=300,
            messages=[{"role": "user", "content": prompt}],
        )
        result = json.loads(resp.content[0].text.strip())
        if "action" not in result:
            raise ValueError("missing action")
    except Exception:
        result = {"action": "add", "target_id": None, "reason": "judge error — defaulting to add"}

    _cache[key] = (now, result)
    if len(_cache) > 500:
        cutoff = now - _CACHE_TTL
        for k in [k for k, (t, _) in list(_cache.items()) if t < cutoff]:
            del _cache[k]

    return result
