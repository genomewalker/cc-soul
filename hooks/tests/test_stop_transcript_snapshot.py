import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

HELPER = Path(__file__).resolve().parents[1] / "stop-transcript-snapshot.py"
SPEC = importlib.util.spec_from_file_location("stop_transcript_snapshot", HELPER)
snapshot_module = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(snapshot_module)

MCP_DIR = Path(__file__).resolve().parents[2] / "chitta-mcp"
sys.path.insert(0, str(MCP_DIR))
import thread_inference  # noqa: E402


class SanitizerDedupTests(unittest.TestCase):
    def test_clean_user_is_the_shared_thread_inference_function(self):
        # hooks/stop-transcript-snapshot.py and chitta-mcp/thread_inference.py
        # used to carry byte-identical copies of this sanitizer. Assert they
        # now resolve to the exact same function object, not just equivalent
        # behavior, so a future edit to one can't silently drift from the other.
        self.assertIs(snapshot_module._clean_user, thread_inference._clean_user_text)


class StopTranscriptSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.transcript = self.root / "rollout.jsonl"
        self.cursor = self.root / "cursor.json"

    def append(self, *rows):
        with self.transcript.open("a") as handle:
            for row in rows:
                handle.write(json.dumps(row) + "\n")

    def test_codex_uses_exact_event_message_and_incremental_cursor(self):
        self.append(
            {
                "type": "response_item",
                "payload": {
                    "type": "message",
                    "role": "user",
                    "content": [{"type": "input_text", "text": "fix the stop hook"}],
                },
            },
            {
                "type": "response_item",
                "payload": {
                    "type": "function_call",
                    "call_id": "call-1",
                    "name": "apply_patch",
                    "arguments": json.dumps({"file_path": "/repo/hooks/stop-core.sh"}),
                },
            },
            {
                "type": "response_item",
                "payload": {
                    "type": "function_call_output",
                    "call_id": "call-1",
                    "output": "done",
                },
            },
            {
                "type": "event_msg",
                "payload": {
                    "type": "token_count",
                    "info": {
                        "total_token_usage": {
                            "input_tokens": 100,
                            "cached_input_tokens": 70,
                            "cache_write_input_tokens": 4,
                            "output_tokens": 20,
                        }
                    },
                },
            },
        )
        first = snapshot_module.build_snapshot(
            self.transcript,
            self.cursor,
            {
                "session_id": "session",
                "turn_id": "turn-1",
                "last_assistant_message": "exact final answer",
            },
        )
        self.assertEqual(first["response"], "exact final answer")
        self.assertEqual(first["tools"], ["apply_patch"])
        self.assertEqual(first["files"], ["/repo/hooks/stop-core.sh"])
        self.assertEqual(first["last_user"], "fix the stop hook")
        self.assertEqual(first["token_usage"]["total_cache_read"], 70)

        snapshot_path = self.root / "snapshot.json"
        snapshot_path.write_text(json.dumps(first))
        snapshot_module._commit(snapshot_path, self.cursor)
        first_offset = first["window"]["next_offset"]

        self.append(
            {
                "type": "response_item",
                "payload": {
                    "type": "message",
                    "role": "user",
                    "content": [{"type": "input_text", "text": "run the tests"}],
                },
            },
            {
                "type": "response_item",
                "payload": {
                    "type": "function_call",
                    "call_id": "call-2",
                    "name": "exec_command",
                    "arguments": json.dumps({"path": "/repo/tests"}),
                },
            },
        )
        second = snapshot_module.build_snapshot(
            self.transcript,
            self.cursor,
            {"session_id": "session", "turn_id": "turn-2", "last_assistant_message": "tests pass"},
        )
        self.assertEqual(second["window"]["start"], first_offset)
        self.assertEqual(second["tools"], ["exec_command"])
        self.assertEqual(second["files"], ["/repo/tests"])
        self.assertEqual(second["user_turns"], ["fix the stop hook", "run the tests"])

    def test_claude_tool_blocks_and_markers(self):
        self.append(
            {"type": "user", "message": {"content": "inspect the hook"}},
            {
                "type": "assistant",
                "message": {
                    "content": [
                        {"type": "text", "text": "working"},
                        {
                            "type": "tool_use",
                            "id": "tool-1",
                            "name": "Read",
                            "input": {"file_path": "/repo/hooks/stop-core.sh"},
                        },
                    ],
                    "usage": {"input_tokens": 40, "output_tokens": 5},
                },
            },
            {
                "type": "user",
                "message": {
                    "content": [
                        {
                            "type": "tool_result",
                            "tool_use_id": "tool-1",
                            "content": "source",
                            "is_error": False,
                        }
                    ]
                },
            },
            {
                "type": "assistant",
                "message": {
                    "content": [
                        {
                            "type": "text",
                            "text": "[SOLUTION] Use one incremental transcript snapshot.",
                        }
                    ],
                    "usage": {"input_tokens": 50, "output_tokens": 8},
                },
            },
        )
        result = snapshot_module.build_snapshot(
            self.transcript,
            self.cursor,
            {"session_id": "claude-session", "last_assistant_message": "final"},
        )
        self.assertEqual(result["tools"], ["Read"])
        self.assertEqual(result["files"], ["/repo/hooks/stop-core.sh"])
        self.assertEqual(result["tool_spans"][0]["output"], "source")
        self.assertEqual(result["token_usage"]["total_input_tokens"], 90)
        self.assertIn("[SOLUTION] Use one incremental transcript snapshot.", result["markers"])

    def test_bootstrap_and_increment_are_bounded(self):
        with self.transcript.open("wb") as handle:
            line = b'{"type":"noise","padding":"' + (b"x" * 1000) + b'"}\n'
            for _ in range(4096):
                handle.write(line)
            handle.write(
                json.dumps(
                    {
                        "type": "response_item",
                        "payload": {
                            "type": "message",
                            "role": "user",
                            "content": [{"type": "input_text", "text": "bounded tail"}],
                        },
                    }
                ).encode()
                + b"\n"
            )
        result = snapshot_module.build_snapshot(
            self.transcript,
            self.cursor,
            {"session_id": "bounded", "last_assistant_message": "ok"},
            bootstrap_bytes=128 * 1024,
            max_increment_bytes=256 * 1024,
        )
        self.assertLessEqual(result["window"]["bytes_scanned"], 256 * 1024)
        self.assertGreater(result["window"]["start"], 0)
        self.assertEqual(result["last_user"], "bounded tail")


if __name__ == "__main__":
    unittest.main()
