#!/usr/bin/env python3
"""hintd_smoke.py — smoke test for a running chitta_hintd.

Assumes a chitta_hintd is already listening on --socket. Exercises:
  - PING -> PONG (cheap liveness, no inference)
  - a clear preference turn -> non-empty hint
  - noise -> empty hint
  - two concurrent requests -> no hang (admission control: at most one inference)

Reuses the production client (_hintd_extract) so this validates the exact path
scripts/hint_realtime.py uses with CHITTA_HINT_BACKEND=hintd.

Usage:
    python3 hintd_smoke.py --socket /path/.hintd.sock
"""

import argparse, socket, sys, threading, time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))
from hint_realtime import _hintd_extract  # reuse the production client


def _raw(sock_path: str, payload: str, timeout: float = 10.0) -> str:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(sock_path)
    s.sendall(payload.encode())
    s.shutdown(socket.SHUT_WR)
    out = b""
    while True:
        b = s.recv(4096)
        if not b:
            break
        out += b
    s.close()
    return out.decode("utf-8", "replace").strip()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--socket", required=True)
    args = ap.parse_args()
    sock = args.socket
    ok = True

    pong = _raw(sock, "PING")
    print(f"[ping]   -> {pong!r}  {'OK' if pong == 'PONG' else 'FAIL'}")
    ok &= pong == "PONG"

    pref = "I always use ripgrep instead of grep, and prefer fd over find for code search."
    hint = _hintd_extract(sock, pref, 12.0)
    print(f"[pref]   -> {hint!r}  {'OK' if hint else 'FAIL (expected a hint)'}")
    ok &= bool(hint)

    noise = _hintd_extract(sock, "ok", 12.0)
    print(f"[noise]  -> {noise!r}  {'OK' if noise == '' else 'WARN (expected empty)'}")

    # Two at once: admission control means at most one inference runs; the other
    # gets an empty response immediately. Both must return within the timeout —
    # the assertion is "no hang", not which one wins.
    results: dict = {}

    def fire(i: int) -> None:
        results[i] = _hintd_extract(sock, pref + f" (run {i})", 12.0)

    t0 = time.monotonic()
    threads = [threading.Thread(target=fire, args=(i,)) for i in range(2)]
    for t in threads:
        t.start()
    for t in threads:
        t.join(20)
    dt = time.monotonic() - t0
    alive = [t for t in threads if t.is_alive()]
    print(f"[concur] -> {results}  {dt:.1f}s  {'OK' if not alive else 'FAIL (hang)'}")
    ok &= not alive

    print("RESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
