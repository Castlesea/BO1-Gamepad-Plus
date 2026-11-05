# BO1 Gamepad Plus

Controller improvements and a borderless window for **Call of Duty: Black Ops (2010)** on PC.

This project **does not include any game files**. You need your own original copy of the game.

## Features

- **Less controller lag:** turns off the console aim acceleration and response curve, and applies radial deadzones to both sticks.
- **D-pad in menus:** makes the D-pad navigate like the arrow keys, while keeping the item slots during gameplay.
- **Borderless window:** `BlackOps Borderless.exe` starts the game at your monitor's resolution with no borders.
- **Live configuration:** `bo1pad.ini` reloads automatically when you save it, even with the game open.

The game reads the controller itself. Nothing presses keys or creates a virtual controller in Windows. An optional mode (`AimMode=mouse`) turns the right stick into mouse movement inside the game. It removes more aim lag, but leaves out aim assist.

## Installation

1. Download the `.zip` from [Releases](../../releases).
2. Extract `xinput1_3.dll`, `bo1pad.ini` and `BlackOps Borderless.exe` into the folder that contains `BlackOps.exe`.
3. Start the game with `BlackOps Borderless.exe` and accept the UAC prompt. It asks for administrator rights so it can start the game and change its window.

The launcher changes `r_fullscreen`, `r_mode`, `vid_xpos` and `vid_ypos` in `players\config.cfg`. You may want to back up that file first.

**Uninstall:** delete the three files. To go back to fullscreen, change it in the game's video options.

## Compatibility

Tested on **a single installation** with a generic XInput controller. Other versions of the game have not been tested.

The tweaks rely on addresses in `BlackOps.exe`. If your version is different, `bo1pad.log` shows `unknown BlackOps.exe build`. In that case, the dvar and D-pad tweaks stay off.

The game only reads the controller in the first XInput slot.

## Configuration

Main options in `bo1pad.ini`:

| Option | What it does |
|---|---|
| `AimMode` | `native` uses the game's aim; `mouse` uses the right stick as a mouse |
| `DpadMenuArrows` | `1` makes the D-pad act as the arrow keys; `0` keeps the original keys |
| `Log` | `1` writes `bo1pad.log` to the game folder |
| `[LeftStick] Deadzone` | Left stick deadzone |
| `[MouseAim] Deadzone` | Right stick deadzone, in both modes |
| `[MouseAim]` | Speed, curve and boost, used in `mouse` mode |
| `[Dvars]` | Values written directly into the game |

## Building

Requires Visual Studio with the C++ tools. Put this folder inside the game folder and run:

```bat
build.bat
```

The compiled files are copied to the parent folder. The icon is optional: if there is a `blackops.ico` in this folder, it goes into the launcher. The game's icon is not part of this repository.

## License

The code in this project is under the [MIT license](LICENSE). It grants no rights to the game itself.

## Disclaimer

Fan project, not affiliated with Activision or Treyarch. Call of Duty is a trademark of Activision.
