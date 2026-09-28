#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 OpenLF2 contributors
"""Stamps one version number into every file that embeds it as metadata.

    tools/set_version.py X.Y.Z

CMakeLists.txt's `project(OpenLF2 VERSION X.Y.Z ...)` is the project's version of record;
dist/build-appimage.sh and dist/build-deb.sh already read it from there with sed. This script
keeps the handful of places that cannot do that (a packaging manifest read by a tool that has
never heard of CMake, or a numeric-only field in a different format) in sync with it instead of
letting them drift, and are the only files it touches:

  - CMakeLists.txt        project(OpenLF2 VERSION ...): the version of record itself.
  - conanfile.py           version = "...": read by `conan export`/`conan install` before CMake
                            ever runs, so it cannot be derived from the CMake file.
  - android/app/build.gradle
                           versionName '...' (shown to players) and versionCode (an integer Play/
                           sideloading compares to decide "is this an update"; bumped by 1 here
                           whenever versionName actually changes, never otherwise).
  - ios/Info.plist         CFBundleShortVersionString (shown to players) and CFBundleVersion (an
                            integer, same role and same bump rule as Android's versionCode).
  - CMakeLists.txt         vita_create_vpk(... VERSION XX.YY ...): PS Vita's SFO TITLE_VERSION
                            field is always exactly two two-digit groups, not semver; this script
                            derives it from X.Y (MAJOR.MINOR, zero-padded, PATCH dropped) since
                            there is no lossless mapping and this is what Vita software
                            conventionally shows.
  - dist/io.github.openlf2.OpenLF2.metainfo.xml
                           <releases>: prepends a dated <release version=".../> entry (today,
                            in the system's local time) so the AppImage's own AppStream metadata
                            (read by desktop-file/software-center tooling, not by the game) shows
                            a version history, only when the version actually changed; older
                            entries are kept, never rewritten, and a no-op re-run of the same
                            version adds nothing.

Not touched, but not drifting either: the Switch NRO's NACP version reads PROJECT_VERSION
directly (CMakeLists.txt's nx_generate_nacp call), so it already tracks this script's own edit.

Not covered, because nothing in the repository currently sets it at all (there is no version
of record to conflict with, only one this script would have to invent): a Windows VERSIONINFO
resource (the project's only .rc file sets just the icon). Extend this script deliberately if
and when that gets a real version field to keep in sync, rather than inventing metadata nothing
reads yet.
"""
import datetime
import pathlib
import re
import sys

VERSION_PATTERN = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")


def replace_once(path, pattern, replacement, flags=0):
    """Substitutes the first and only match of `pattern` in `path`, or fails loudly."""
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    matches = len(re.findall(pattern, text, flags))
    if matches != 1:
        raise SystemExit("set_version: expected exactly one match of %r in %s, found %d" %
                          (pattern, path, matches))
    with open(path, "w", encoding="utf-8") as f:
        f.write(re.sub(pattern, replacement, text, count=1, flags=flags))


def bump_integer_field(path, pattern, changed):
    """Increments the integer captured by `pattern` (one group) in `path` by 1, only if a
    version string elsewhere in the same file actually changed; otherwise leaves it untouched
    so re-running with the same version is a no-op."""
    if not changed:
        return
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    match = pattern.search(text)
    if not match:
        raise SystemExit("set_version: expected to find %r in %s" % (pattern.pattern, path))
    bumped = str(int(match.group(1)) + 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text[:match.start(1)] + bumped + text[match.end(1):])
    return bumped


def add_appstream_release(path, version, changed):
    """Prepends a `<release version="..." date="..."/>` entry to the AppStream `<releases>`
    list (newest first, per the AppStream spec), only if the version actually changed and no
    entry for it exists yet; otherwise leaves the file untouched, so a no-op re-run never moves
    an already-recorded release's date."""
    if not changed:
        return
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    if re.search(r'<release version="%s"' % re.escape(version), text):
        return
    entry = '    <release version="%s" date="%s"/>\n' % (version, datetime.date.today().isoformat())
    new_text, matches = re.subn(r"(<releases>\n)", r"\1" + entry, text, count=1)
    if matches != 1:
        raise SystemExit("set_version: could not find <releases> in %s" % path)
    with open(path, "w", encoding="utf-8") as f:
        f.write(new_text)


def current_cmake_version(root):
    with open(root / "CMakeLists.txt", "r", encoding="utf-8") as f:
        text = f.read()
    match = re.search(r"project\(OpenLF2 VERSION (\d+\.\d+\.\d+) LANGUAGES CXX\)", text)
    if not match:
        raise SystemExit("set_version: could not find the project() version in CMakeLists.txt")
    return match.group(1)


def main(argv):
    if len(argv) != 2 or not VERSION_PATTERN.match(argv[1]):
        print(__doc__)
        return 2
    new_version = argv[1]
    root = pathlib.Path(__file__).resolve().parent.parent

    old_version = current_cmake_version(root)
    changed = old_version != new_version
    major, minor, _patch = new_version.split(".")
    vita_version = "%02d.%02d" % (int(major), int(minor))

    replace_once(root / "CMakeLists.txt",
                 r"project\(OpenLF2 VERSION \d+\.\d+\.\d+ LANGUAGES CXX\)",
                 "project(OpenLF2 VERSION %s LANGUAGES CXX)" % new_version)
    replace_once(root / "conanfile.py",
                 r'version = "\d+\.\d+\.\d+"',
                 'version = "%s"' % new_version)
    replace_once(root / "android/app/build.gradle",
                 r"versionName '\d+\.\d+\.\d+'",
                 "versionName '%s'" % new_version)
    android_code = bump_integer_field(root / "android/app/build.gradle",
                                       re.compile(r"versionCode (\d+)"), changed)
    replace_once(root / "ios/Info.plist",
                 r"<key>CFBundleShortVersionString</key><string>\d+\.\d+\.\d+</string>",
                 "<key>CFBundleShortVersionString</key><string>%s</string>" % new_version)
    ios_build = bump_integer_field(root / "ios/Info.plist",
                                    re.compile(r"<key>CFBundleVersion</key><string>(\d+)</string>"), changed)
    replace_once(root / "CMakeLists.txt",
                 r"vita_create_vpk\(openlf2\.vpk OLF200001 eboot\.bin VERSION \d{2}\.\d{2} NAME",
                 "vita_create_vpk(openlf2.vpk OLF200001 eboot.bin VERSION %s NAME" % vita_version)
    add_appstream_release(root / "dist/io.github.openlf2.OpenLF2.metainfo.xml", new_version, changed)

    print("set_version: %s -> %s" % (old_version, new_version))
    if changed:
        print("set_version: bumped android versionCode to %s, ios CFBundleVersion to %s, "
              "added an AppStream release entry dated %s" %
              (android_code, ios_build, datetime.date.today().isoformat()))
    else:
        print("set_version: version unchanged, versionCode/CFBundleVersion/AppStream releases left alone")
    print("set_version: Vita SFO version set to %s (MAJOR.MINOR only; PS Vita has no patch field)" %
          vita_version)
    print("set_version: not touched (no version field exists yet to keep in sync, see this "
          "script's own docstring): Windows .rc")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
