"""Reads config.toml as a document: sections in file order, each with the comment block
above it and the keys under it. Which are live follows source.mode, source.format and
store.type. It reports the file, not any running process."""

from __future__ import annotations

import tomllib
from pathlib import Path
from typing import Any

_SELECTED: dict[tuple[str, str], str] = {
    ("source", "mode"): "source.{}",
    ("source", "format"): "source.{}",
    ("store", "type"): "{}",
}

_SELECTABLE: dict[tuple[str, str], list[str]] = {
    ("source", "mode"): ["source.file", "source.rabbit"],
    ("source", "format"): ["source.csv"],
    ("store", "type"): ["redis"],
}


def _blocks(text: str) -> list[dict[str, Any]]:
    """Every section of the raw file in order, with its comment block and its keys."""
    sections: list[dict[str, Any]] = []
    comment: list[str] = []

    for raw in text.splitlines():
        line = raw.strip()

        if line.startswith("#"):
            if "####" in line or "---" in line:
                comment.clear()
            elif line.strip("#").strip():
                comment.append(line.strip("#").strip())
            continue

        if not line:
            continue

        if line.startswith("["):
            sections.append({"name": line.strip("[]").strip(),
                             "help": " ".join(comment).strip(),
                             "keys": []})
            comment.clear()
            continue

        if "=" in line and sections:
            key, _, rest = line.partition("=")
            value, _, note = rest.partition("#")
            sections[-1]["keys"].append({"key": key.strip(),
                                         "value": value.strip(),
                                         "help": note.strip()})
            comment.clear()

    return sections


def _live(doc: dict[str, Any]) -> tuple[set[str], dict[str, str]]:
    """The sections the settings select, and why each unselected one is off."""
    live: set[str] = set()
    reasons: dict[str, str] = {}

    for (table, key), shape in _SELECTED.items():
        chosen = doc.get(table, {}).get(key)
        if chosen is None:
            continue
        picked = shape.format(chosen)
        live.add(picked)
        for name in _SELECTABLE[(table, key)]:
            if name != picked:
                reasons[name] = f'{table}.{key} is "{chosen}"'

    return live, reasons


def document(path: Path) -> dict[str, Any]:
    """The whole file as the config screen shows it."""
    text = path.read_text(encoding="utf-8")
    live, reasons = _live(tomllib.loads(text))

    sections = [{**section,
                 "active": section["name"] not in reasons,
                 "reason": reasons.get(section["name"], "")}
                for section in _blocks(text)]

    return {"path": str(path), "sections": sections, "selected": sorted(live)}
