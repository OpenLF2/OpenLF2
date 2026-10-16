# Contributing

Implementation is in progress. Inspect the working tree and recent commits before changing code.

Describe the observable behavior a change adds or fixes, and separate established facts from
hypotheses. Keep changes small enough that reviewers can compare behavior, adapter boundaries,
and script contracts.

A feature contribution should include:

- Its purpose and affected C++/Lua boundary.
- Relevant validation results and any remaining differences.
- Updated API, modding, or build documentation when the contract changes.

Use synthetic resources for public examples. Keep original assets, binaries, and private
gameplay captures out of commits.

Existing-cache builds are authorized; interactive sessions and visual/audio comparisons
are the owner's. Do not install dependencies.

## Release notes

Keep dated releases in `CHANGELOG.md` newest first, using
`## [VERSION] - YYYY-MM-DD` headings. After editing release notes, regenerate
the AppStream release history, including all versions and their descriptions:

```sh
python3 tools/update_appstream_releases.py
```

Use `--check` to check synchronization without writing files. The script preserves
component metadata outside `<releases>` and omits an undated `[Unreleased]` section.
