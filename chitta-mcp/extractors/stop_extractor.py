#!/usr/bin/env python3
"""
Structured learning extractor for stop-hook distillation.

Reads one assistant turn from a transcript, calls a local ollama endpoint
(discovered via /tmp/ollama-server-*.url files, falling back to localhost)
and parses JSONL output per the 6-kind schema in STRUCTURED_EXTRACTOR_DESIGN.md §2.

Valid lines are emitted as `observe` events through the chitta CLI
(source=distillation). Malformed lines are appended to
$MIND_PATH/.distill_parse_errors.jsonl for later inspection.
"""

import glob
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error


VALID_KINDS = {"lesson", "gotcha", "decision", "preference", "correction", "pattern"}
VALID_SCOPES = {"project", "global", "partnership"}
HEAD_CHARS = 12000
TAIL_CHARS = 12000
LLM_TIMEOUT = 180
MAX_OBJECTS = 8
MODEL_DEFAULT = os.environ.get("CHITTA_DISTILL_MODEL", "qwen2.5:14b-instruct")

SYSTEM_PROMPT = """You extract structured learnings from one assistant turn. Output
ONLY newline-delimited JSON objects matching the schema below. No prose,
no markdown. Empty output is valid when nothing was learned.

SCHEMA:
{kind, title, content, scope, realm, confidence, evidence{turn_indices,quote},
 affect{valence,arousal}, flags[], granularity}

RULES:
- kind in {lesson,gotcha,decision,preference,correction,pattern}
- content uses SSL v0.4: "[domain:abbr] subj->act->result G:N F:FLAG A:v,a"
- Tier-1 kinds (gotcha, pattern, lesson-with-code) append "[e] <verbatim>"
- quote MUST be a substring of the input conversation
- Skip anything the user corrected - the corrected version is the learning
- Confidence: 0.9 for explicit statements, 0.7 for inferred, <0.6 -> drop
"""


def discover_endpoint():
    for path in glob.glob("/tmp/ollama-server-*.url"):
        try:
            with open(path) as f:
                url = f.read().strip()
            if not url:
                continue
            req = urllib.request.Request(f"{url}/v1/models")
            with urllib.request.urlopen(req, timeout=3) as r:
                if b"data" in r.read():
                    return url
        except Exception:
            continue
    try:
        with urllib.request.urlopen("http://localhost:11434/v1/models", timeout=3) as r:
            if b"data" in r.read():
                return "http://localhost:11434"
    except Exception:
        pass
    return ""


def truncate(text, head=HEAD_CHARS, tail=TAIL_CHARS):
    if len(text) <= head + tail:
        return text
    return f"{text[:head]}\n\n[... truncated {len(text) - head - tail} chars ...]\n\n{text[-tail:]}"


def read_last_assistant_turn(transcript_path):
    """Return (user_prompt, assistant_response) for the last assistant message."""
    user_prompt = ""
    assistant_response = ""
    try:
        with open(transcript_path) as f:
            lines = f.readlines()
    except Exception:
        return "", ""
    last_asst_idx = None
    for i in range(len(lines) - 1, -1, -1):
        try:
            rec = json.loads(lines[i])
        except Exception:
            continue
        role = rec.get("role") or rec.get("message", {}).get("role")
        if role == "assistant" and last_asst_idx is None:
            content = rec.get("message", {}).get("content", [])
            parts = [c.get("text", "") for c in content if isinstance(c, dict) and c.get("type") == "text"]
            assistant_response = "\n".join(parts)
            last_asst_idx = i
        elif role == "user" and last_asst_idx is not None:
            content = rec.get("message", {}).get("content", [])
            if isinstance(content, list):
                parts = [c.get("text", "") for c in content if isinstance(c, dict) and c.get("type") == "text"]
                user_prompt = "\n".join(parts)
            elif isinstance(content, str):
                user_prompt = content
            break
    return truncate(user_prompt, 4000, 4000), truncate(assistant_response)


def build_user_prompt(session_id, realm, turn_index, user_prompt, assistant_response):
    return (
        f"INPUT:\n  session_id: {session_id}\n  realm: {realm}\n"
        f"  last_turn_index: {turn_index}\n"
        f"  user_prompt: {user_prompt}\n"
        f"  assistant_response: {assistant_response}\n\n"
        f"OUTPUT: JSONL, max {MAX_OBJECTS} objects."
    )


def call_llm(endpoint, model, user_msg):
    req_body = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": user_msg},
        ],
        "temperature": 0.2,
        "max_tokens": 2048,
    }).encode()
    req = urllib.request.Request(
        f"{endpoint}/v1/chat/completions",
        data=req_body,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=LLM_TIMEOUT) as r:
            payload = json.loads(r.read())
        return payload["choices"][0]["message"]["content"]
    except Exception as e:
        sys.stderr.write(f"[stop_extractor] LLM call failed: {e}\n")
        return ""


def validate(obj):
    if not isinstance(obj, dict):
        return False
    if obj.get("kind") not in VALID_KINDS:
        return False
    if not obj.get("title") or not obj.get("content"):
        return False
    if obj.get("scope") not in VALID_SCOPES:
        return False
    conf = obj.get("confidence")
    if not isinstance(conf, (int, float)) or conf < 0.6:
        return False
    return True


def parse_jsonl(raw, error_log_path):
    good = []
    for line in raw.splitlines():
        s = line.strip()
        if not s or s.startswith("```"):
            continue
        try:
            obj = json.loads(s)
        except Exception as e:
            _log_error(error_log_path, s, f"json: {e}")
            continue
        if not validate(obj):
            _log_error(error_log_path, s, "schema")
            continue
        good.append(obj)
        if len(good) >= MAX_OBJECTS:
            break
    return good


def _log_error(path, line, reason):
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "a") as f:
            f.write(json.dumps({"ts": int(time.time()), "reason": reason, "line": line[:500]}) + "\n")
    except Exception:
        pass


def kind_to_category(kind):
    return {
        "lesson": "wisdom",
        "gotcha": "gotcha",
        "decision": "decision",
        "preference": "preference",
        "correction": "correction",
        "pattern": "pattern",
    }.get(kind, "wisdom")


def emit(obj, realm_default):
    chitta_bin = os.environ.get("CHITTA_BIN", os.path.expanduser("~/.claude/bin/chitta"))
    category = kind_to_category(obj["kind"])
    realm = obj.get("realm") or realm_default or "brahman"
    tags = f"source=distillation,kind={obj['kind']},scope={obj.get('scope','project')},realm={realm}"
    args = [
        chitta_bin, "observe",
        "--category", category,
        "--title", obj["title"][:80],
        "--content", obj["content"],
        "--confidence", str(obj.get("confidence", 0.85)),
        "--tags", tags,
    ]
    try:
        subprocess.run(args, timeout=10, check=False, capture_output=True)
    except Exception as e:
        sys.stderr.write(f"[stop_extractor] observe failed: {e}\n")


def run(session_id, turn_index, transcript_path, realm):
    mind_path = os.environ.get("CHITTA_DB_PATH", os.path.expanduser("~/.claude/mind"))
    error_log = os.path.join(mind_path, ".distill_parse_errors.jsonl")

    if not transcript_path or not os.path.isfile(transcript_path):
        sys.stderr.write(f"[stop_extractor] no transcript: {transcript_path}\n")
        return 0

    endpoint = discover_endpoint()
    if not endpoint:
        sys.stderr.write("[stop_extractor] no ollama endpoint\n")
        return 0

    user_prompt, assistant_response = read_last_assistant_turn(transcript_path)
    if not assistant_response or len(assistant_response) < 10:
        return 0

    msg = build_user_prompt(session_id, realm, turn_index, user_prompt, assistant_response)
    raw = call_llm(endpoint, MODEL_DEFAULT, msg)
    if not raw.strip():
        sys.stderr.write("[stop_extractor] no learnings\n")
        return 0

    learnings = parse_jsonl(raw, error_log)
    for obj in learnings:
        emit(obj, realm)
    return len(learnings)


def main():
    if len(sys.argv) < 5:
        sys.stderr.write("usage: stop_extractor.py <session_id> <turn_index> <transcript_path> <realm>\n")
        sys.exit(2)
    session_id = sys.argv[1]
    try:
        turn_index = int(sys.argv[2])
    except ValueError:
        turn_index = 0
    transcript_path = sys.argv[3]
    realm = sys.argv[4]
    n = run(session_id, turn_index, transcript_path, realm)
    sys.stderr.write(f"[stop_extractor] emitted {n} learnings\n")


if __name__ == "__main__":
    main()
