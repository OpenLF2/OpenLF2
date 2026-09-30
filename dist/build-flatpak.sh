#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build from the current checkout and export a Flatpak repository with extra data.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
manifest=dist/io.github.openlf2.OpenLF2.yml
mkdir -p build out
builder=(flatpak-builder)
metadata_validator=(appstreamcli)
flatpak_version=$(flatpak --version)
flatpak_version=${flatpak_version#Flatpak }
if [[ $(printf '%s\n' 1.15.6 "$flatpak_version" | sort -V | head -n1) != 1.15.6 ]]; then
    # Older host tools cannot parse --device=input; the builder app bundles newer tools.
    flatpak install --user --noninteractive flathub org.flatpak.Builder
    builder=(flatpak run --user org.flatpak.Builder)
    metadata_validator=(flatpak run --user --command=appstreamcli org.flatpak.Builder)
fi
"${builder[@]}" --user --install-deps-from=flathub --assumeyes --force-clean \
    --state-dir=build/flatpak-state --repo=build/flatpak-repo \
    build/flatpak "$manifest"

# Validate the installed layout and executable inside the build sandbox.
flatpak build build/flatpak sh -eu -c '
    test -f /app/bin/scripts/base/package.manifest
    test -f /app/share/applications/io.github.openlf2.OpenLF2.desktop
    test -f /app/share/metainfo/io.github.openlf2.OpenLF2.metainfo.xml
    test -f /app/share/icons/hicolor/scalable/apps/io.github.openlf2.OpenLF2.svg
    test -x /app/bin/apply_extra
    test ! -e /app/extra/LF2_v2.0a.exe
    desktop-file-validate /app/share/applications/io.github.openlf2.OpenLF2.desktop
    openlf2-launch --help
'
"${metadata_validator[@]}" validate --no-net \
    "$PWD/build/flatpak/files/share/metainfo/io.github.openlf2.OpenLF2.metainfo.xml"

# Single-file bundles cannot download extra data; distribute the repository instead.
tar -czf out/OpenLF2-flatpak-repo.tar.gz -C build flatpak-repo
(cd out && sha256sum OpenLF2-flatpak-repo.tar.gz > OpenLF2-flatpak-repo.tar.gz.sha256)
