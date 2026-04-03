#!/usr/bin/env python3
"""
Generate large SSL distillation training corpus from Wikipedia articles.

For each article chunk:
  chosen:   claude -p → SSL lines extracted from the text
  rejected: random soul memory from chitta pool (wrong content)

Output: training_data_web.jsonl (DPO format)

Usage:
    python 01c_web_corpus.py --n-articles 500 --output training_data_web.jsonl
"""

import argparse
import json
import random
import re
import subprocess
import sys
import time
from pathlib import Path

CHITTA_BIN = Path.home() / ".claude" / "bin" / "chitta"

SYSTEM_PROMPT = (
    "You are a memory distiller. Given a text passage, extract the key learnings "
    "in SSL v0.2 format: [TYPE] [domain] subject→action→result\n"
    "Types: SOLUTION, GOTCHA, DECISION, PATTERN, PREFERENCE, FAILURE\n"
    "Be concise. Use SSL arrows (→). Max 6 lines.\n"
    "Output ONLY SSL lines, one per line. No preamble, no explanation.\n"
    "If the text has no clear learnings, output a single PATTERN line."
)

SSL_PATTERN = re.compile(
    r'^\[(SOLUTION|GOTCHA|DECISION|PATTERN|PREFERENCE|FAILURE)\]'
)

# Wikipedia topic seeds — diverse coverage
TOPIC_SEEDS = [
    # Software / CS
    "Compiler optimization", "Cache memory", "Distributed computing",
    "Unix philosophy", "Software design pattern", "Garbage collection computer science",
    "Version control", "Continuous integration", "Database index",
    "TCP/IP", "Public-key cryptography", "Memory management",
    # Biology / Science
    "Natural selection", "CRISPR", "Protein folding", "Cell signaling",
    "Antibiotic resistance", "Epigenetics", "Mitochondria",
    # Math / Stats
    "Bayesian inference", "Monte Carlo method", "Principal component analysis",
    "Gradient descent", "Fourier transform", "Information theory",
    # Engineering
    "Feedback control", "Signal-to-noise ratio", "Reliability engineering",
    "Fault tolerance", "Load balancing",
    # Misc cognitive / process
    "Cognitive bias", "Dunning-Kruger effect", "Scientific method",
    "Root cause analysis", "Technical debt", "Occam's razor",
]


def fetch_wikipedia_article(title: str) -> str | None:
    """Fetch plain text of a Wikipedia article via the API."""
    try:
        import urllib.request
        import urllib.parse
        url = (
            "https://en.wikipedia.org/w/api.php?"
            + urllib.parse.urlencode({
                "action": "query",
                "titles": title,
                "prop": "extracts",
                "explaintext": True,
                "exsectionformat": "plain",
                "format": "json",
                "redirects": True,
            })
        )
        req = urllib.request.Request(
            url,
            headers={"User-Agent": "ssl-distiller-corpus/1.0 (training data generation)"},
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = json.loads(resp.read())
        pages = data["query"]["pages"]
        page = next(iter(pages.values()))
        return page.get("extract", "")
    except Exception as e:
        print(f"  [warn] fetch failed for '{title}': {e}", file=sys.stderr)
        return None


def chunk_text(text: str, chunk_words: int = 300, overlap: int = 50) -> list[str]:
    """Split text into overlapping word chunks."""
    words = text.split()
    chunks = []
    step = chunk_words - overlap
    for start in range(0, len(words), step):
        chunk = " ".join(words[start:start + chunk_words])
        if len(chunk.split()) >= 80:   # skip tiny chunks
            chunks.append(chunk)
    return chunks


LLM_MODEL = "gemma4:26b"


def _discover_endpoint() -> str | None:
    """Discover GPU endpoint: cached URL files → localhost."""
    import glob, urllib.request
    for path in glob.glob("/tmp/ollama-server-*.url"):
        try:
            url = open(path).read().strip()
            urllib.request.urlopen(url + "/v1/models", timeout=3)
            return url
        except Exception:
            pass
    try:
        urllib.request.urlopen("http://localhost:11434/v1/models", timeout=3)
        return "http://localhost:11434"
    except Exception:
        return None


def call_llm(text: str, timeout: int = 60) -> str | None:
    """Call LLM via HTTP (Ollama/vLLM)."""
    endpoint = _discover_endpoint()
    if not endpoint:
        print("  [warn] No GPU endpoint found", file=sys.stderr)
        return None
    prompt = SYSTEM_PROMPT + "\n\n## Text\n\n" + text
    import urllib.request
    req_body = json.dumps({
        "model": LLM_MODEL,
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.3,
        "max_tokens": 4096,
    }).encode()
    try:
        req = urllib.request.Request(
            endpoint + "/v1/chat/completions",
            data=req_body,
            headers={"Content-Type": "application/json"},
        )
        resp = urllib.request.urlopen(req, timeout=timeout)
        data = json.loads(resp.read().decode())
        return data["choices"][0]["message"]["content"].strip() or None
    except Exception as e:
        print(f"  [warn] LLM error: {e}", file=sys.stderr)
        return None


def load_rejection_pool() -> list[str]:
    """Load soul memories as rejection pool via chitta recall."""
    try:
        r = subprocess.run(
            [str(CHITTA_BIN), "recall",
             "--query", "solution gotcha decision pattern preference failure",
             "--limit", "2000"],
            capture_output=True, text=True, errors="replace", timeout=60,
        )
        lines = [
            l.strip() for l in r.stdout.splitlines()
            if l.strip() and not l.startswith("Found ")
        ]
        print(f"  {len(lines)} rejection pool memories loaded")
        return lines
    except Exception as e:
        print(f"  [warn] chitta recall failed: {e}", file=sys.stderr)
        return []


def main():
    parser = argparse.ArgumentParser(description="Generate SSL training corpus from Wikipedia")
    parser.add_argument("--n-articles", type=int, default=200,
                        help="Number of Wikipedia articles to process")
    parser.add_argument("--chunks-per-article", type=int, default=3,
                        help="Max chunks per article")
    parser.add_argument("--output", default="training_data_web.jsonl")
    parser.add_argument("--append", action="store_true",
                        help="Append to existing output file")
    args = parser.parse_args()

    output_path = Path(args.output)
    print(f"Generating web SSL corpus → {output_path}")
    print(f"Target: {args.n_articles} articles × {args.chunks_per_article} chunks")

    # Load rejection pool
    print("\nLoading rejection pool...")
    rejection_pool = load_rejection_pool()

    # Expand topic list with random Wikipedia "Special:Random" fetches if needed
    topics = list(TOPIC_SEEDS)
    if args.n_articles > len(topics):
        # Add extra random topics from Wikipedia random page endpoint
        try:
            import urllib.request
            for _ in range(args.n_articles - len(topics)):
                url = "https://en.wikipedia.org/w/api.php?action=query&list=random&rnnamespace=0&rnlimit=10&format=json"
                req = urllib.request.Request(url, headers={"User-Agent": "ssl-distiller-corpus/1.0"})
                with urllib.request.urlopen(req, timeout=10) as resp:
                    data = json.loads(resp.read())
                for page in data["query"]["random"]:
                    topics.append(page["title"])
        except Exception as e:
            print(f"  [warn] random topics fetch failed: {e}")

    random.shuffle(topics)
    topics = topics[:args.n_articles]

    # Generate pairs
    pairs = []
    skipped = 0
    mode = "a" if args.append else "w"

    with open(output_path, mode) as out_f:
        for i, topic in enumerate(topics):
            print(f"\n[{i+1}/{len(topics)}] {topic}")
            text = fetch_wikipedia_article(topic)
            if not text or len(text.split()) < 100:
                skipped += 1
                continue

            chunks = chunk_text(text)[:args.chunks_per_article]
            for j, chunk in enumerate(chunks):
                chosen_raw = call_llm(chunk)
                if not chosen_raw:
                    skipped += 1
                    continue

                # Filter to valid SSL lines
                ssl_lines = [
                    l for l in chosen_raw.splitlines()
                    if SSL_PATTERN.match(l.strip())
                ]
                if not ssl_lines:
                    skipped += 1
                    continue

                chosen = "\n".join(ssl_lines[:6])

                # Rejected: random pool memories (wrong topic)
                if rejection_pool:
                    rejected = "\n".join(random.sample(rejection_pool, min(4, len(rejection_pool))))
                else:
                    rejected = "[PATTERN] [unrelated] wrong→content→here @nowhere"

                pair = {
                    "prompt": chunk,
                    "chosen": chosen,
                    "rejected": rejected,
                    "source": f"wikipedia:{topic}",
                }
                pairs.append(pair)
                out_f.write(json.dumps(pair) + "\n")
                out_f.flush()

                print(f"  chunk {j+1}: {len(ssl_lines)} SSL lines → {chosen[:80]}...")

            if (i + 1) % 20 == 0:
                print(f"\n--- {i+1}/{len(topics)}: {len(pairs)} pairs, {skipped} skipped ---\n")

            time.sleep(0.3)   # be polite to Wikipedia API

    print(f"\nDone: {len(pairs)} pairs, {skipped} skipped → {output_path}")
    print(f"File size: {output_path.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()
