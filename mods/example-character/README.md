# Example character mod

`--mod mods/example-character` adds **Test Fighter** (record ID 60) to the
character picker. Its `assets/` directory contains an authored plain-text
character header, BMP sprite and portrait, and WAV voice. The mod inherits one
base fighter's moves and collision frames, then uses its own image and sound
for those frames. No original assets are included in the mod.
Choose Previous from Random in the Versus picker to find it quickly.

The `asset=` lines in the manifest mount those files at
`mods/example-character/...`; the catalog and object-data decorators use those
paths to add a playable record without editing base scripts. For a fully new
move set, replace the inherited frames with frames defined in the mod's text
file. The example still needs the player's installer for the base game and moves.
