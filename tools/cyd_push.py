#!/usr/bin/env python3
"""Pushes a built firmware.bin to the board over the network.

PlatformIO runs this for `pio run -t upload` (upload_protocol = custom). The
image goes over HTTP with an HMAC of a one-time challenge, keyed by the update
key. Standard library only, Python 3.9 or newer.

macOS blocks PlatformIO's own Python from the local network, but a curl it
starts works, so every network call goes through curl. The HMAC and the hash
are done here.
"""
import hashlib
import hmac
import os
import re
import secrets
import shlex
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
KEY_LENGTH = 20
MAX_BUSY_RETRIES = 3

IMAGE_DESC_BYTES = 256
DESC_MAGIC = bytes([0x32, 0x54, 0xCD, 0xAB])
DESC_MAGIC_OFFSET = 32
DESC_PROJECT_OFFSET = 80
DESC_PROJECT_BYTES = 32


def normalize_key(text: str) -> str:
    """Drops spaces and dashes, uppercases, reads O as 0 and I or L as 1."""
    key = "".join(text.split()).replace("-", "").upper()
    key = key.replace("O", "0").replace("I", "1").replace("L", "1")
    if len(key) != KEY_LENGTH or any(c not in ALPHABET for c in key):
        raise ValueError("the update key is 20 characters from 0-9 and A-Z without I, L, O and U")
    return key


def new_key() -> str:
    """A random key written XXXX-XXXX-XXXX-XXXX-XXXX."""
    key = "".join(secrets.choice(ALPHABET) for _ in range(KEY_LENGTH))
    return "-".join(key[i:i + 4] for i in range(0, KEY_LENGTH, 4))


def auth_message(challenge: str, length: int, sha256_hex: str, project: str, commit_time: int) -> bytes:
    """The text the HMAC covers; the board builds the same bytes."""
    return ("cyd-update-1\n%s\n%d\n%s\n%s\n%d\n" %
            (challenge, length, sha256_hex, project, commit_time)).encode()


def read_project(image: bytes) -> str:
    """The project name in an image's app description, as image_desc_read finds it."""
    if len(image) < IMAGE_DESC_BYTES or image[0] != 0xE9 or \
            image[DESC_MAGIC_OFFSET:DESC_MAGIC_OFFSET + 4] != DESC_MAGIC:
        raise ValueError("not an app image")
    field = image[DESC_PROJECT_OFFSET:DESC_PROJECT_OFFSET + DESC_PROJECT_BYTES]
    end = field.find(b"\0")
    if end <= 0:
        raise ValueError("not an app image")
    return field[:end].decode("ascii", "replace")


def board_host(environ, host_file: Path) -> str:
    """CYD_HOST, else the first line of the host file."""
    host = environ.get("CYD_HOST", "").strip()
    if host:
        return host
    try:
        lines = host_file.read_text().splitlines()
    except OSError:
        lines = []
    if lines and lines[0].strip():
        return lines[0].strip()
    raise RuntimeError("no board address: set CYD_HOST (for example cyd-e5d0.local) or put it in %s" % host_file)


def check_challenge(reply: str, host: str) -> str:
    """The challenge, which the board sends as 32 lowercase hex digits."""
    if not re.fullmatch(r"[0-9a-f]{32}", reply):
        raise RuntimeError("%s did not answer with a challenge (32 hex digits): "
                           "CYD_HOST may point at another device" % host)
    return reply


def busy_seconds(reply: str) -> Optional[int]:
    """N from a `busy N` reply, else None."""
    parts = reply.split()
    if len(parts) == 2 and parts[0] == "busy" and parts[1].isdigit():
        return int(parts[1])
    return None


def config_dir() -> Path:
    return Path.home() / ".config" / "cyd"


def key_file_path() -> Path:
    return Path(os.environ.get("CYD_KEY_FILE") or config_dir() / "update_key")


def load_key() -> str:
    """Reads the key file, refusing a missing one or one others can read."""
    path = key_file_path()
    try:
        mode = path.stat().st_mode
    except OSError:
        script = shlex.quote(str(Path(__file__).resolve()))
        raise RuntimeError("no update key at %s: run `python3 %s --new-key`" % (path, script))
    # Windows has no such permission bits to check.
    if os.name != "nt" and mode & 0o077:
        raise RuntimeError("%s is readable by others: run `chmod 600 %s`" % (path, path))
    return normalize_key(path.read_text())


def write_new_key(force: bool) -> None:
    path = key_file_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    if force:
        # A new file, so neither an old file's mode nor a symlink carries over.
        try:
            path.unlink()
        except FileNotFoundError:
            pass
    try:
        fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise RuntimeError("%s already exists: use --new-key --force to replace it" % path)
    if hasattr(os, "fchmod"):
        os.fchmod(fd, 0o600)  # whatever the umask took away
    key = new_key()
    with os.fdopen(fd, "w") as handle:
        handle.write(key + "\n")
    try:
        subprocess.run(["pbcopy"], input=key.encode(), check=True)
        copied = "and copied to the clipboard"
    except (OSError, subprocess.CalledProcessError):
        copied = "(pbcopy failed, so use --show-key to read it)"
    print("new update key saved to %s %s" % (path, copied))
    print("enter it on the board's phone setup page")


def commit_time() -> int:
    root = Path(__file__).resolve().parent.parent
    try:
        out = subprocess.run(["git", "-C", str(root), "log", "-1", "--format=%ct"],
                             capture_output=True, text=True, check=True).stdout.strip()
        return int(out)
    except (OSError, subprocess.CalledProcessError, ValueError):
        raise RuntimeError("cannot read the commit time of HEAD from git in %s" % root)


def curl(args) -> "tuple[str, int]":
    """Runs curl; returns the reply text and HTTP status."""
    # --ipv4: the board has no IPv6 address, and macOS waits 5 s for an AAAA
    # answer for a .local name before it settles for the IPv4 one.
    try:
        done = subprocess.run(["curl", "-sS", "--ipv4"] + list(args) + ["-w", "\n%{http_code}"],
                              capture_output=True, text=True)
    except OSError:
        raise RuntimeError("curl was not found on PATH")
    if done.returncode != 0:
        raise RuntimeError("curl failed: %s" % (done.stderr.strip() or "exit %d" % done.returncode))
    body, _, code = done.stdout.rpartition("\n")
    return body.strip(), int(code) if code.isdigit() else 0


def push(firmware: str) -> int:
    key = load_key()
    host = board_host(os.environ, config_dir() / "host")
    image = Path(firmware).read_bytes()
    project = read_project(image)
    sha = hashlib.sha256(image).hexdigest()
    when = commit_time()
    extra = shlex.split(os.environ.get("CYD_CURL_EXTRA", ""))
    base = "http://%s" % host

    reply, status = "", 0
    for attempt in range(MAX_BUSY_RETRIES + 1):
        challenge, status = curl(["--max-time", "10", base + "/update/challenge"])
        if status != 200:
            print("%s: %s" % (host, challenge))
            return 1
        challenge = check_challenge(challenge, host)
        message = auth_message(challenge, len(image), sha, project, when)
        digest = hmac.new(key.encode(), message, hashlib.sha256).hexdigest()
        reply, status = curl(["--max-time", "180", "-H", "Expect:",
                              "-H", "X-Cyd-Challenge: " + challenge,
                              "-H", "X-Cyd-Project: " + project,
                              "-H", "X-Cyd-Sha256: " + sha,
                              "-H", "X-Cyd-Commit-Time: %d" % when,
                              "-H", "X-Cyd-Hmac: " + digest,
                              "--data-binary", "@" + firmware] + extra + [base + "/update"])
        wait = busy_seconds(reply)
        if wait is None or attempt == MAX_BUSY_RETRIES:
            break
        print("%s: %s; trying again in %d s" % (host, reply, wait + 1))
        time.sleep(wait + 1)
    print("%s: %s" % (host, reply))
    return 0 if status == 200 else 1


def main(argv) -> int:
    args = list(argv[1:])
    try:
        if args and args[0] == "--new-key":
            write_new_key("--force" in args[1:])
            return 0
        if args == ["--show-key"]:
            key = normalize_key(key_file_path().read_text())
            print("-".join(key[i:i + 4] for i in range(0, KEY_LENGTH, 4)))
            return 0
        if len(args) != 1 or args[0].startswith("--"):
            print("usage: cyd_push.py FIRMWARE.bin | --new-key [--force] | --show-key", file=sys.stderr)
            return 2
        return push(args[0])
    except (RuntimeError, ValueError, OSError) as error:
        print("cyd_push: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
