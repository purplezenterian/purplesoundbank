#!/bin/bash
# Installs PSB Bridge for your user (no password needed) and clears the download warning.
cd "$(dirname "$0")"
mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/PSB Bridge.vst3" "$HOME/Library/Audio/Plug-Ins/Components/PSB Bridge.component"
cp -R "PSB Bridge.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
cp -R "PSB Bridge.component" "$HOME/Library/Audio/Plug-Ins/Components/"
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/PSB Bridge.vst3" "$HOME/Library/Audio/Plug-Ins/Components/PSB Bridge.component" 2>/dev/null
echo ""
echo "PSB Bridge installed. Restart your DAW and rescan plugins."
echo "You can close this window."
