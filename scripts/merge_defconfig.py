#!/usr/bin/env python3
"""Apply or verify a small Kconfig fragment without losing the base config."""

from __future__ import annotations

import argparse
import os
import re
import tempfile
from pathlib import Path


SETTING_RE = re.compile(
    r"^(?:CONFIG_(?P<set>[A-Za-z0-9_]+)=.*|# CONFIG_(?P<unset>[A-Za-z0-9_]+) is not set)$"
)


def setting_name(line: str) -> str | None:
    match = SETTING_RE.match(line.rstrip("\r\n"))
    if not match:
        return None
    return match.group("set") or match.group("unset")


def read_fragment(path: Path) -> tuple[list[str], dict[str, str]]:
    ordered: list[str] = []
    settings: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        name = setting_name(raw)
        if name is None:
            continue
        if name in settings:
            raise ValueError(f"duplicate fragment setting: CONFIG_{name}")
        ordered.append(name)
        settings[name] = raw
    if not ordered:
        raise ValueError(f"fragment has no Kconfig settings: {path}")
    return ordered, settings


def merge(base_text: str, ordered: list[str], settings: dict[str, str]) -> str:
    output: list[str] = []
    seen: set[str] = set()
    newline = "\r\n" if "\r\n" in base_text else "\n"

    for raw in base_text.splitlines():
        name = setting_name(raw)
        if name in settings:
            if name not in seen:
                output.append(settings[name])
                seen.add(name)
            continue
        output.append(raw)

    missing = [name for name in ordered if name not in seen]
    if missing:
        if output and output[-1]:
            output.append("")
        output.append("# 2026 openVela AI contest overlay")
        output.extend(settings[name] for name in missing)

    return newline.join(output) + newline


def atomic_write(path: Path, text: str) -> None:
    mode = path.stat().st_mode
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("apply", "check"))
    parser.add_argument("base", type=Path)
    parser.add_argument("fragment", type=Path)
    args = parser.parse_args()

    ordered, settings = read_fragment(args.fragment)
    with args.base.open("r", encoding="utf-8", newline="") as stream:
        base_text = stream.read()
    merged = merge(base_text, ordered, settings)

    if args.mode == "check":
        if base_text != merged:
            missing = [
                f"CONFIG_{name}"
                for name in ordered
                if settings[name] not in base_text.splitlines()
            ]
            print("contest config mismatch: " + ", ".join(missing))
            return 1
        print(f"contest config verified: {args.base}")
        return 0

    if merged != base_text:
        atomic_write(args.base, merged)
        print(f"contest config applied: {args.base}")
    else:
        print(f"contest config already applied: {args.base}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
