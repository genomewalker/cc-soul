import random
import sys
import unittest
import zlib
from pathlib import Path

MCP_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MCP_DIR))

from mdl_gate import CHUNK_BYTES, judge  # noqa: E402

_FILLER = [
    "the daemon restarted cleanly after the config reload without dropping any in-flight sessions and metrics stayed flat.",
    "hooks read the socket path from CHITTA_DB_PATH before falling back to the default mind directory location on disk.",
    "the mcp server needed a restart after the plugin marketplace re-cloned and replaced the symlinks with a stale copy.",
    "observe stores the memory with category solution and links it to the current episode via a derived_from triplet edge.",
    "the distill hook truncates conversations over 80k chars keeping the first 25k and last 55k characters of the transcript.",
    "smart_recall scored the keyword leg twice as high after the realm scoping fix landed in production last week.",
    "the outcome ledger appends one jsonl line per bash exit code, correlated later by session id and a sliding time window.",
]


def _make_evidence(wisdom: str, n_chunks: int, lines_per_chunk: int = 400) -> str:
    """Build a multi-chunk transcript-like evidence blob with `wisdom` recurring
    once per chunk amid unrelated filler, so it spans >32KB (multiple chunks)."""
    rng = random.Random(11)
    chunks = []
    for _ in range(n_chunks):
        lines = [
            f"{rng.choice(_FILLER)} turn={i} id={rng.randint(1000, 9999)}"
            for i in range(lines_per_chunk)
        ]
        lines.insert(len(lines) // 2, wisdom)
        chunks.append("\n".join(lines))
    return "\n".join(chunks)


class JudgeTests(unittest.TestCase):
    def test_compressive_schema_accepted(self):
        # Wisdom lifted verbatim from evidence, recurring across several
        # >32KB chunks amid unrelated filler: the schema genuinely explains
        # away part of the evidence's cost.
        wisdom = "cmake --build build --parallel finishes the chitta rebuild about 4x faster than the default single-threaded build."
        evidence = _make_evidence(wisdom, n_chunks=4)
        self.assertGreater(len(evidence.encode()), CHUNK_BYTES * 3)

        result = judge(wisdom, evidence)
        self.assertTrue(result["accept"])
        self.assertGreaterEqual(result["saving"], result["margin"])

    def test_unrelated_wisdom_rejected(self):
        wisdom_in_evidence = "cmake --build build --parallel finishes the chitta rebuild about 4x faster than the default single-threaded build."
        evidence = _make_evidence(wisdom_in_evidence, n_chunks=4)

        unrelated = "the user prefers dark mode and tabs over spaces and dislikes verbose commit message footers in every repo."
        result = judge(unrelated, evidence)
        self.assertFalse(result["accept"])

    def test_empty_inputs_do_not_crash(self):
        result = judge("", "")
        self.assertIn("accept", result)
        self.assertFalse(result["accept"])

        result = judge("some wisdom", "")
        self.assertIn("accept", result)

        result = judge("", "some evidence that is long enough to matter here")
        self.assertIn("accept", result)
        self.assertFalse(result["accept"])

    def test_chunking_sums_across_multiple_chunks(self):
        # Evidence spans 3 chunks; verify judge's chunked totals match an
        # independent per-chunk recomputation using the same zdict scheme.
        line = "the daemon restarts cleanly after a config reload\n"
        repeats = (CHUNK_BYTES * 2) // len(line) + 10
        evidence = line * repeats
        self.assertGreater(len(evidence.encode()), CHUNK_BYTES * 2)

        wisdom = "config-reload: daemon restarts cleanly, no manual intervention needed"
        result = judge(wisdom, evidence)

        e_bytes = evidence.encode("utf-8")
        w_bytes = wisdom.encode("utf-8")

        def compress(data, zdict=None):
            co = (
                zlib.compressobj(9, zlib.DEFLATED, -15, 9, 0, zdict)
                if zdict
                else zlib.compressobj(9, zlib.DEFLATED, -15, 9, 0)
            )
            return len(co.compress(data) + co.flush())

        expected_c_w = compress(w_bytes)
        expected_c_e = 0
        expected_c_e_given_w = 0
        for i in range(0, len(e_bytes), CHUNK_BYTES):
            chunk = e_bytes[i : i + CHUNK_BYTES]
            expected_c_e += compress(chunk)
            expected_c_e_given_w += compress(chunk, zdict=w_bytes)
        expected_c_we = expected_c_w + expected_c_e_given_w

        self.assertEqual(result["c_e"], expected_c_e)
        self.assertEqual(result["c_we"], expected_c_we)
        self.assertEqual(result["saving"], expected_c_e - expected_c_we)


if __name__ == "__main__":
    unittest.main()
