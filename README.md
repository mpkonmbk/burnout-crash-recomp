# Burnout CRASH! — PC recompilation

An unofficial native Windows port of the Xbox 360 (XBLA) game **Burnout CRASH!** (2011), built by
statically recompiling the original PowerPC executables to C++ with the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). No emulator runs at play time: the game's own code
is compiled to x86-64 and runs on ReXGlue's Xbox 360 runtime (Direct3D 12 graphics, audio, input).

> [!IMPORTANT]
> **This project is vibe-coded.** Everything in it (the reverse engineering, codegen configs, native hooks,
> build scripts, tools and this README) was written by **Claude** (Anthropic's AI, model Claude Opus 5.5,
> working through Claude Code) in conversation with the repository owner, who set the goals, supplied the
> game dump and tested the results. It has been played on exactly one PC. Expect rough edges.

**This repository contains no game content.** There are no game files, no executables and no recompiled
code here. You need your own copy of Burnout CRASH!, dumped from your own Xbox 360, and the code is
generated on your machine.

## Quick start

You need Windows 10/11 x64, a Direct3D 12 GPU, and your Burnout CRASH! package file. That's the file
(with a long hexadecimal name) in `Content\0000000000000000\58410B5D\000D0000\` on your console's drive.
Tools such as Horizon or Velocity can export it from a USB drive the console used.

1. Download this repository (**Code → Download ZIP**, then extract it, or `git clone`), and open
   PowerShell in its folder.
2. Run the setup script with the path to your package file:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\setup.ps1 -Package "D:\path\to\your\package"
   ```

3. Play: `out\build\win-amd64-relwithdebinfo\burnoutcrash.exe`. Adding `-Run` to step 2 starts the game
   when the build finishes.

`setup.ps1` does everything else, one step at a time:

- It installs the missing build tools with winget after asking you first: Visual Studio 2022 Build Tools
  with C++ (about 3–4 GB), LLVM/Clang, CMake and Ninja. Windows may ask for admin rights.
- It downloads the ReXGlue SDK and verifies its checksum.
- It verifies your package, extracts it to `game\`, and checks that it's the supported version.
- It builds the game. The first build takes several minutes and several GB of RAM.

Plan for about 10 GB of free disk space if the build tools still need installing, or 3 GB otherwise.
Re-running the script skips the steps that are already done, so after updating the repository just run
`.\setup.ps1` again (without `-Package`).

## Status

- Boots through the intro to the title screen and main menu, with the full game unlocked (no trial).
- PLAY and saving have worked in limited testing.
- Not much else has been tested yet. Bug reports with a `crash_trace.txt` are welcome (see
  [Troubleshooting](#troubleshooting)).

## Supported version

Only this exact build works. The configs refer to fixed addresses in it, and the build checks the hashes:

| | |
|---|---|
| Game | Burnout CRASH!, Xbox Live Arcade |
| Title ID / media ID | `58410B5D` / `734ABC74`, title update 3 |
| `DEFAULT.XEX` SHA-256 | `f2389f9b8655e6432a78db8a4adc4cf524e5c56a6a92d013b6f0fb0452d4b425` |
| `DLL\CRASH.DLL.XEX` SHA-256 | `bf66a2d38cf9a79dfe20558d289edb96b9f2346bf344613946a4ada897298338` |

## Manual setup

Use this instead of `setup.ps1` if you'd rather install and run each step yourself.

### Requirements

- Windows 10/11 x64, a Direct3D 12 GPU, about 3 GB of free disk space (game files, generated code and build output)
- Visual Studio 2022 Build Tools with the C++ workload (for the Windows SDK and C++ libraries)
- LLVM/Clang 20 or newer, CMake 3.25+, Ninja
- ReXGlue SDK nightly **0.10.0.24-dev.gbd833a2**:
  [rexglue-sdk-0.10.0.24-dev.gbd833a2-win-amd64.zip](https://github.com/rexglue/rexglue-sdk/releases/download/nightly-20261002-bd833a2a/rexglue-sdk-0.10.0.24-dev.gbd833a2-win-amd64.zip)

The tools can be installed with winget (`setup.ps1` runs the same commands):

```powershell
winget install -e --id LLVM.LLVM
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

### Steps

1. **Get the SDK.** Extract the ReXGlue zip into `third_party\rexglue-sdk\`, so that
   `third_party\rexglue-sdk\win-amd64\bin\rexglue.exe` exists. Alternatively, set `REXGLUE_SDK` or pass
   `-SdkDir` to the build script.
2. **Get the game files.** Copy your Burnout CRASH! package off your console. It's the file in
   `Content\0000000000000000\58410B5D\000D0000\` on the console's drive. Then extract it into `game\`:

   ```powershell
   .\tools\extract-package.ps1 -Package <path to the package file>
   ```

   The script checks every block of the package against its hash tree first, so a damaged copy is
   reported instead of silently producing broken files. Any other STFS extractor works too, as long as
   `game\DEFAULT.XEX` and `game\DLL\CRASH.DLL.XEX` end up in place.
3. **Build.**

   ```powershell
   .\build.ps1
   ```

   The first build generates about 375 MB of C++ from the game and compiles it. That takes a few minutes
   and several GB of RAM; `-Jobs` limits parallel compiles if memory is tight.

## Running

```powershell
out\build\win-amd64-relwithdebinfo\burnoutcrash.exe
```

The game files are found automatically (a `game` folder next to the exe, otherwise this project's `game\`).
Useful options:

| Option | Effect |
|---|---|
| `--fullscreen` | Start in fullscreen |
| `--resolution=1080p` | Render resolution and window size (`720p`, `1440p`, `4k`, `WxH`) |
| `--mnk_mode` | Keyboard as controller |
| `--license_mask=0` | Run as the trial version (the full game is the default) |
| `--game_data_root=<dir>` | Use game files from another folder |
| `--log_file=<path> --log_level=debug` | Detailed log |

In-game, **F3** shows the debug overlay, **F4** the settings and **`** (backtick) the console.

Saves go to `Documents\burnoutcrash\`.

The game's online features (EA account, Autolog, leaderboards) don't work, since the services shut down
years ago. Decline the sign-in prompts.

## Troubleshooting

- If the game crashes, it writes `crash_trace.txt` next to the exe, listing the recompiled functions on
  the stack (`sub_XXXXXXXX` is the original Xbox 360 address). Include it, and the newest file in the
  `logs\` folder next to the exe, when reporting a bug.
- *"…is a different build than this project supports"* means your dump isn't the version in
  [Supported version](#supported-version).

## How it works

[NOTES.md](NOTES.md) has the technical details: the project layout, and every fix applied on top of plain
ReXGlue codegen. Some highlights:

- The game is two executables. `DEFAULT.XEX` is the native engine, and `CRASH.DLL.XEX` is the game logic,
  written in C# and compiled ahead of time with EA's *EASharp*.
- 140 switch statements in the C# code use a jump-table pattern ReXGlue doesn't detect. They're declared
  in `config/crash_dll.toml`, with a small hook for the cases where the index register is overwritten.
- About 375 functions in `DEFAULT.XEX` are only reachable through data or tail calls, and are declared
  in `config/default.toml`.
- A few guest functions are replaced with native versions in `src/guest_overrides.cpp`: a stack walker
  and an Xbox Live lookup that crashed the game when PLAY was selected.

## Legal

This is a fan project, not affiliated with or endorsed by Electronic Arts, Criterion Games or Microsoft.
Burnout and Burnout CRASH! are trademarks of Electronic Arts Inc. The repository contains only original
code and configuration under the [MIT license](LICENSE). You must own the game and dump it yourself.
Don't share game files, generated code or builds made from them.

The ReXGlue SDK is a separate project under its own BSD 3-Clause license and isn't included here.
