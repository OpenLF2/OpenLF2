# Changelog

All notable changes to this project are documented in this file.

## [0.9.3] - 2026-10-11

### Added
- Optional expanded battlefield layout with support for parallax backgrounds
- Desktop window size, position and maximized state are saved and restored before showing the window; Wayland controls positioning

### Changed
- Semi-transparent on-screen controls, with no overlapping diagonal highlights on the digipad
- Options menu adjusts its layout to the available settings
- High-density rendering enabled across platforms
- Updated credits, website links and development notice
- TLS certificate verification disabled for installer downloads; installer SHA-256 verification remains enabled

### Fixed
- Keyboard and gamepad access to website links and the recording folder
- Recording help and special move links
- iOS pause button in widescreen, info bar and application exit
- Template background loading with incomplete BMP palettes
- Compiler signedness warnings; compiler warnings are now treated as errors

## [0.9.2] - 2026-10-10

### Fixed
- Black sprites with the OpenGL ES renderer
- On-screen gamepad is now also hidden when mouse force mode is active
- Launch script for ROCKNIX

## [0.9.1] - 2026-10-02

### Added
- On-screen gamepad (digipad): configurable, drawn as an overlay (also in the letterboxed area), enabled by default and larger
- On-screen special buttons and a pause button
- Touch tolerance for on-screen controls
- Support for multiple screen orientations
- Flatpak packaging
- Portmaster build

### Changed
- Linear filter is now the default (and fixed)
- All options are now shown in the config

## [0.9.0] - 2026-09-28

- Initial release
