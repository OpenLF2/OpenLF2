#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 OpenLF2 contributors
"""Copy every dated release in CHANGELOG.md into AppStream's <releases> block.

Supports the repository's Markdown format: `## [VERSION] - YYYY-MM-DD`,
`### Category`, paragraphs, and flat unordered lists with wrapped lines.
An optional `## [Unreleased]` section is omitted until it has a release date.
Other component metadata is preserved byte for byte. Python 3.9+; no dependencies.
"""

import argparse
import datetime
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
RELEASE = re.compile(r"^## \[([^\[\]\s]+)\] - (\d{4}-\d{2}-\d{2})$")
BULLET = re.compile(r"^[-*+]\s+(.+)$")
RELEASES_BLOCK = re.compile(
    r"^(?P<indent>[ \t]*)<releases\b[^>]*?(?:/>|>.*?</releases>)[ \t]*$",
    re.MULTILINE | re.DOTALL,
)


def plain_text(text):
    """Render simple inline Markdown as text, using only AppStream block markup."""
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1 (\2)", text)
    text = re.sub(r"`([^`]+)`", r"\1", text)
    text = re.sub(r"\*\*(.+?)\*\*|__(.+?)__", lambda m: m[1] or m[2], text)
    return text


def changelog_releases(markdown):
    releases = ET.Element("releases")
    versions = set()
    description = None
    block = None
    listing = None
    seen_section = False
    previous_date = None

    for number, raw in enumerate(markdown.splitlines(), 1):
        line = raw.strip()
        if line.startswith("## "):
            seen_section = True
            description = block = listing = None
            if line.lower() in ("## [unreleased]", "## unreleased"):
                continue
            match = RELEASE.fullmatch(line)
            if not match:
                raise ValueError(f"line {number}: expected '## [VERSION] - YYYY-MM-DD'")
            version, date = match.groups()
            parsed_date = datetime.date.fromisoformat(date)
            if version in versions:
                raise ValueError(f"line {number}: duplicate version {version}")
            if previous_date is not None and parsed_date > previous_date:
                raise ValueError(f"line {number}: releases must be ordered newest first")
            previous_date = parsed_date
            versions.add(version)
            release = ET.SubElement(releases, "release", version=version, date=date)
            description = ET.SubElement(release, "description")
            continue

        if description is None:
            # Ignore the document introduction and the undated Unreleased notes.
            continue
        if not line:
            block = listing = None
            continue
        if line.startswith("### "):
            ET.SubElement(description, "p").text = plain_text(line[4:]) + ":"
            block = listing = None
            continue
        if line.startswith(("#", "```", "~~~", ">", "|")) or re.match(r"\d+[.)]\s", line):
            raise ValueError(f"line {number}: unsupported changelog Markdown: {line!r}")
        bullet = BULLET.fullmatch(line)
        if bullet:
            if raw[:1].isspace():
                raise ValueError(f"line {number}: nested lists are not supported")
            if listing is None:
                listing = ET.SubElement(description, "ul")
            block = ET.SubElement(listing, "li")
            block.text = plain_text(bullet[1])
        elif listing is not None:
            block.text += " " + plain_text(line)
        else:
            listing = None
            if block is None or block.tag != "p":
                block = ET.SubElement(description, "p")
                block.text = plain_text(line)
            else:
                block.text += " " + plain_text(line)

    if not seen_section or not len(releases):
        raise ValueError("changelog has no dated releases")
    for release in releases:
        if not len(release.find("description")):
            raise ValueError(f"release {release.get('version')} has no notes")
    return releases


def update_metadata(changelog, metadata):
    releases = changelog_releases(changelog)
    component = ET.fromstring(metadata)
    if component.tag != "component" or len(component.findall("releases")) != 1:
        raise ValueError("metadata must contain one component with one <releases> block")
    if component.find("releases").get("type", "embedded") != "embedded":
        raise ValueError("metadata must use embedded releases")
    matches = list(RELEASES_BLOCK.finditer(metadata))
    if len(matches) != 1:
        raise ValueError("expected one <releases> block on its own lines")
    match = matches[0]
    ET.indent(releases, space="  ")
    rendered = ET.tostring(releases, encoding="unicode", short_empty_elements=False)
    rendered = "\n".join(match["indent"] + line for line in rendered.splitlines())
    updated = metadata[:match.start()] + rendered + metadata[match.end():]
    ET.fromstring(updated)
    return updated, len(releases)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--changelog", type=Path, default=ROOT / "CHANGELOG.md")
    parser.add_argument("--metainfo", type=Path, default=ROOT / "dist/io.github.openlf2.OpenLF2.metainfo.xml")
    parser.add_argument("--check", action="store_true", help="report stale metadata without changing it")
    args = parser.parse_args(argv)
    try:
        metadata = args.metainfo.read_text(encoding="utf-8")
        updated, count = update_metadata(args.changelog.read_text(encoding="utf-8"), metadata)
        if updated == metadata:
            print(f"AppStream releases are up to date ({count} releases).")
            return 0
        if args.check:
            print("AppStream releases are stale; run tools/update_appstream_releases.py.", file=sys.stderr)
            return 1
        args.metainfo.write_text(updated, encoding="utf-8")
        print(f"Updated {count} AppStream releases from {args.changelog}.")
        return 0
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"update_appstream_releases: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
