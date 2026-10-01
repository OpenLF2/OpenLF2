#!/bin/bash
# PortMaster launcher for OpenLF2. Sits next to the openlf2/ folder in the port's zip:
#   OpenLF2.sh
#   openlf2/openlf2.aarch64, openlf2/scripts/, openlf2/libs.aarch64/, openlf2/licenses/
# Draft: untested on a device.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

# Find PortMaster on whichever firmware this is.
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/openlf2"
# config.json and the original game's installer (LF2_v2.0a.exe) live here, so they survive
# updates of the port.
CONFDIR="$GAMEDIR/conf"
mkdir -p "$CONFDIR"
cd "$GAMEDIR" || exit 1

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$CONFDIR"
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# The game needs the original installer. It can download it itself when the device is online;
# otherwise copy LF2_v2.0a.exe into $CONFDIR (see README.md).
if [ ! -f "$CONFDIR/LF2_v2.0a.exe" ]; then
  echo "LF2_v2.0a.exe not found in $CONFDIR; the game will offer to download it."
fi

# OpenLF2 reads the controller through SDL itself; gptokeyb only provides the exit hotkey
# (hotkey + start), so it has no key map.
$GPTOKEYB "openlf2.${DEVICE_ARCH}" &
pm_platform_helper "$GAMEDIR/openlf2.${DEVICE_ARCH}"

# The bundled libSDL3.so.0 runs on the device's own SDL2, whose GLES renderer is the one that
# works everywhere; set OPENLF2_RENDERER (e.g. software) to try another.
./openlf2.${DEVICE_ARCH} --config-dir "$CONFDIR" --default-controller --renderer "${OPENLF2_RENDERER:-opengles2}"

pm_finish
