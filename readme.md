# ffxiv-unstrip-acls

Restores `PROCESS_VM_WRITE` (and `PROCESS_VM_READ`) to your user when launching FFXIV through the official launcher. Other launchers are unaffected.

Fixes Discord overlay and other things which broke when SE decided to do this garbage.

## Install

1. Download DLL from releases page
2. Navigate to your game directory, where `game` and `boot` folders are located
3. Open the `boot` folder
4. Paste the `winmm.dll` into that directory
5. That's it.