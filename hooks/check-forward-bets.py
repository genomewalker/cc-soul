#!/usr/bin/env python3
"""Check open forward bets and optionally mark resolved."""

import argparse
import json
import os
import subprocess
from datetime import datetime, timezone

GRADE_REALM = "project:cc-soul"


def recall_bets(limit=20):
    env = {**os.environ, "SQZ_NO_DEDUP": "1"}
    r = subprocess.run(
        [
            "chitta",
            "recall",
            "--query",
            "status:open forward bet prediction",
            "--realm",
            GRADE_REALM,
            "--limit",
            str(limit),
            "--json",
        ],
        capture_output=True,
        text=True,
        timeout=30,
        env=env,
    )
    if r.returncode != 0:
        return []
    return json.loads(r.stdout).get("results", [])


def main():
    ap = argparse.ArgumentParser(description="Forward bet checker")
    ap.add_argument("--resolve", metavar="ID", help="Memory ID to mark resolved")
    ap.add_argument("--outcome", choices=["pass", "fail", "partial"], default="pass")
    ap.add_argument("--limit", type=int, default=20)
    a = ap.parse_args()

    if a.resolve:
        content = f"[bet-resolved] id={a.resolve} outcome={a.outcome} ts={datetime.now(timezone.utc).isoformat()}"
        subprocess.run(
            [
                "chitta",
                "remember",
                "--content",
                content,
                "--realm",
                GRADE_REALM,
                "--type",
                "wisdom",
                "--tags",
                "bet-resolved",
                "--visibility",
                "1",
            ],
            capture_output=True,
            timeout=10,
        )
        print(f"Marked {a.resolve} as {a.outcome}")
        return

    bets = recall_bets(a.limit)
    if not bets:
        print("No open bets found.")
        return
    print(f"{'ID':>20}  {'Text preview'}")
    print("-" * 80)
    for b in bets:
        print(f"{b['id']:>20}  {b.get('text', '')[:60]}")
    print(f"\n{len(bets)} open bets found. Use --resolve <id> --outcome pass|fail|partial")


if __name__ == "__main__":
    main()
