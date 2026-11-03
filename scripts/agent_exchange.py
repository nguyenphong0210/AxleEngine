"""Local, file-backed Codex/Gemini inbox. Does not invoke either model."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXCHANGE = ROOT / "docs/agents/CODEX_GEMINI_EXCHANGE.json"
AUTHORS = {"Codex", "Gemini"}
KINDS = {"ACK", "QUESTION", "ANSWER", "DELIVERY", "REVIEW", "HANDOFF", "STATUS"}


def incoming(message, agent):
    return message["from"] != agent and message["to"] in {agent, "Both"}


def validate(data):
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise ValueError("Unsupported exchange schema")
    if not isinstance(data.get("current_task"), dict) or not data["current_task"].get("id"):
        raise ValueError("Missing current task card")
    if (not isinstance(data.get("last_read"), dict)
            or set(data["last_read"]) != AUTHORS or not isinstance(data.get("messages"), list)):
        raise ValueError("Invalid read cursors or message list")
    previous = {}
    for message in data["messages"]:
        required = {"id", "from", "to", "type", "task", "reply_to", "base_commit", "timestamp_utc", "body"}
        if not isinstance(message, dict) or not required.issubset(message):
            raise ValueError("Incomplete message")
        if not isinstance(message["id"], str) or not message["id"] or message["id"] in previous:
            raise ValueError("Empty or duplicate message ID")
        if (any(not isinstance(message[key], str) for key in ("from", "to", "type"))
                or message["from"] not in AUTHORS or message["to"] not in AUTHORS | {"Both"}
                or message["type"] not in KINDS):
            raise ValueError("Invalid message author, recipient or type")
        if any(not isinstance(message[key], str) or not message[key].strip()
               for key in ("task", "base_commit", "timestamp_utc", "body")):
            raise ValueError("Empty or non-text message fields")
        if message["reply_to"] is not None and (not isinstance(message["reply_to"], str)
                                               or message["reply_to"] not in previous):
            raise ValueError("Reply must refer to an earlier message ID")
        previous[message["id"]] = message
    for agent, cursor in data["last_read"].items():
        if cursor is not None and (not isinstance(cursor, str) or cursor not in previous
                                   or not incoming(previous[cursor], agent)):
            raise ValueError("Read cursor must reference an incoming message for that agent")
    return data


def read(path):
    return validate(json.loads(Path(path).read_text(encoding="utf-8-sig")))


@contextmanager
def writer_lock(path):
    path = Path(path).resolve()
    lock = path.with_name(path.name + ".lock")
    try:
        descriptor = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as error:
        raise ValueError(f"Exchange busy: {lock}. Re-read/retry; do not overwrite another writer.") from error
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(json.dumps({"pid": os.getpid(), "created_utc": datetime.now(timezone.utc).isoformat()}))
        yield
    finally:
        lock.unlink()


def write_atomic(path, data):
    path = Path(path).resolve()
    validate(data)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=path.parent,
                                         prefix=".exchange-", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def post(path, author, recipient, kind, body, base_commit, reply_to=None, task=None):
    if author not in AUTHORS or recipient not in AUTHORS | {"Both"} or kind not in KINDS:
        raise ValueError("Unknown author, recipient or message type")
    if not isinstance(body, str) or not body.strip():
        raise ValueError("Message body is empty")
    with writer_lock(path):
        data = read(path)
        timestamp = datetime.now(timezone.utc)
        message = {
            "id": f"CG-{timestamp:%Y%m%dT%H%M%S%fZ}-{author.upper()}-{secrets.token_hex(3)}",
            "from": author, "to": recipient, "type": kind,
            "task": task or data["current_task"]["id"], "reply_to": reply_to,
            "base_commit": base_commit, "timestamp_utc": timestamp.isoformat(), "body": body,
        }
        data["messages"].append(message)
        write_atomic(path, data)
        return message["id"]


def inbox(path, agent, unread=False, mark_read=False):
    if agent not in AUTHORS:
        raise ValueError("Unknown inbox agent")

    def select(data):
        messages = [message for message in data["messages"] if incoming(message, agent)]
        cursor = data["last_read"][agent]
        if unread and cursor is not None:
            position = next(i for i, message in enumerate(messages) if message["id"] == cursor)
            messages = messages[position + 1:]
        return messages

    if mark_read:
        with writer_lock(path):
            data = read(path)
            messages = select(data)
            if messages:
                data["last_read"][agent] = messages[-1]["id"]
                write_atomic(path, data)
    else:
        messages = select(read(path))
    return messages


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exchange-file", type=Path, default=DEFAULT_EXCHANGE,
                        help="Alternate exchange for an isolated test or checkout")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    commands.add_parser("validate")
    get = commands.add_parser("inbox")
    get.add_argument("--agent", choices=sorted(AUTHORS), required=True)
    get.add_argument("--unread", action="store_true")
    get.add_argument("--mark-read", action="store_true")
    send = commands.add_parser("post")
    send.add_argument("--from-agent", choices=sorted(AUTHORS), required=True)
    send.add_argument("--to", choices=sorted(AUTHORS | {"Both"}), required=True)
    send.add_argument("--type", choices=sorted(KINDS), required=True)
    send.add_argument("--body-file", type=Path, required=True)
    send.add_argument("--reply-to")
    send.add_argument("--task")
    send.add_argument("--base-commit")
    args = parser.parse_args(argv)
    try:
        if args.command == "post":
            base = args.base_commit or subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
            print(post(args.exchange_file, args.from_agent, args.to, args.type,
                       args.body_file.read_text(encoding="utf-8-sig"), base, args.reply_to, args.task))
        elif args.command == "inbox":
            messages = inbox(args.exchange_file, args.agent, args.unread, args.mark_read)
            for message in messages:
                print(f"{message['id']} | {message['from']} -> {message['to']} | {message['type']} | {message['task']}")
                print(f"Reply to: {message['reply_to']} | Base: {message['base_commit']}")
                print(message["body"] + "\n")
            if not messages:
                print("No matching messages.")
        elif args.command == "status":
            print(json.dumps(read(args.exchange_file)["current_task"], ensure_ascii=False, indent=2))
        else:
            read(args.exchange_file)
            print("Exchange schema and message references: PASS")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Exchange error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
