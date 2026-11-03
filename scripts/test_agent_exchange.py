"""Isolated transport tests: no fake Gemini messages are posted to the live exchange."""
import json
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest

import agent_exchange as exchange


class ExchangeTests(unittest.TestCase):
    def setUp(self):
        root = exchange.ROOT / "build"
        root.mkdir(exist_ok=True)
        self.scratch = tempfile.TemporaryDirectory(prefix="agent-exchange-test-", dir=root)
        resolved = Path(self.scratch.name).resolve()
        self.assertEqual(resolved.parent, root.resolve())
        self.assertTrue(resolved.name.startswith("agent-exchange-test-"))
        self.addCleanup(self.scratch.cleanup)
        self.path = resolved / "TEST_ONLY_EXCHANGE.json"
        self.initial = {"schema_version": 1, "current_task": {"id": "TEST_ONLY"},
                        "last_read": {"Codex": None, "Gemini": None}, "messages": []}
        self.path.write_text(json.dumps(self.initial), encoding="utf-8")

    def test_both_authors_reply_and_preserve_task(self):
        first = exchange.post(self.path, "Codex", "Gemini", "HANDOFF", "Please reply.", "test-base")
        reply = exchange.post(self.path, "Gemini", "Codex", "ANSWER", "Đã đọc; câu trả lời thử nghiệm.", "test-base", first)
        self.assertEqual(exchange.inbox(self.path, "Codex")[0]["id"], reply)
        self.assertEqual(exchange.inbox(self.path, "Gemini")[0]["id"], first)
        data = exchange.read(self.path)
        self.assertEqual(data["messages"][1]["reply_to"], first)
        self.assertEqual(data["current_task"], self.initial["current_task"])
        self.assertEqual(data["last_read"], self.initial["last_read"])

    def test_cursor_changes_only_reader_and_keeps_unread(self):
        first = exchange.post(self.path, "Codex", "Gemini", "QUESTION", "First question", "base")
        exchange.inbox(self.path, "Gemini", mark_read=True)
        data = exchange.read(self.path)
        self.assertEqual(data["last_read"], {"Codex": None, "Gemini": first})
        second = exchange.post(self.path, "Codex", "Gemini", "STATUS", "Second update", "base")
        self.assertEqual([m["id"] for m in exchange.inbox(self.path, "Gemini", unread=True)], [second])
        self.assertEqual(exchange.read(self.path)["last_read"]["Gemini"], first)

    def test_invalid_reply_does_not_change_bytes(self):
        before = self.path.read_bytes()
        with self.assertRaises(ValueError):
            exchange.post(self.path, "Gemini", "Codex", "ACK", "Test", "base", "MISSING")
        self.assertEqual(self.path.read_bytes(), before)
        self.assertFalse(self.path.with_name(self.path.name + ".lock").exists())
        self.assertEqual(list(self.path.parent.glob(".exchange-*.tmp")), [])

    def test_busy_writer_is_not_overwritten(self):
        before = self.path.read_bytes()
        with exchange.writer_lock(self.path):
            with self.assertRaises(ValueError):
                exchange.post(self.path, "Codex", "Gemini", "STATUS", "Test", "base")
        self.assertEqual(self.path.read_bytes(), before)

    def test_cli_round_trip(self):
        body = self.path.parent / "body.txt"
        content = "\u0110\u00e3 \u0111\u1ecdc; isolated CLI reply.\nSecond line."
        body.write_text(content, encoding="utf-8-sig")

        def run(*args):
            return subprocess.run([sys.executable, str(exchange.ROOT / "scripts/agent_exchange.py"),
                                   "--exchange-file", str(self.path), *args],
                                  capture_output=True, encoding="utf-8", check=True).stdout

        first = run("post", "--from-agent", "Codex", "--to", "Gemini", "--type", "HANDOFF",
                    "--body-file", str(body), "--base-commit", "test-base").strip()
        self.assertIn(content, run("inbox", "--agent", "Gemini"))
        reply = run("post", "--from-agent", "Gemini", "--to", "Codex", "--type", "ACK",
                    "--reply-to", first, "--body-file", str(body), "--base-commit", "test-base").strip()
        self.assertIn(reply, run("inbox", "--agent", "Codex"))
        self.assertIn("PASS", run("validate"))
        self.assertEqual(exchange.read(self.path)["last_read"], self.initial["last_read"])

    def test_malformed_field_types_are_rejected(self):
        exchange.post(self.path, "Codex", "Gemini", "STATUS", "Test", "base")
        for field in ("from", "to", "type", "reply_to"):
            data = exchange.read(self.path)
            data["messages"][0][field] = []
            with self.subTest(field=field), self.assertRaises(ValueError):
                exchange.validate(data)

    def test_invalid_author_and_duplicate_ids_fail(self):
        with self.assertRaises(ValueError):
            exchange.post(self.path, "SomeoneElse", "Gemini", "ACK", "Test", "base")
        exchange.post(self.path, "Codex", "Gemini", "STATUS", "Test", "base")
        data = exchange.read(self.path)
        data["messages"].append(dict(data["messages"][0]))
        before = self.path.read_bytes()
        with self.assertRaises(ValueError):
            exchange.write_atomic(self.path, data)
        self.assertEqual(self.path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
