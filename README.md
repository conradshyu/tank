# Tank Battalion - native Windows implementation

A self-contained C++17, Win32/GDI tank-defense game inspired by the gameplay of
[Namco's 1980 Tank Battalion](https://en.wikipedia.org/wiki/Tank_Battalion).
This is an independent implementation, not the original arcade ROM or an emulator.
All graphics are drawn in code and all sound effects are synthesized locally;
no original game artwork, recordings, ROMs, or external assets are included.

## Quick start

1. Extract **all** of `tank.zip` to a writable folder.
2. Run `tank\bin\x64\Release\TankBattalion.exe` to play the included 64-bit build.
3. Press **Enter** to start.

The executable uses a statically linked C++ runtime: no separate Visual C++
redistributable or installation is needed. Windows 10/11 is the supported target.
The game needs neither a network connection nor administrator privileges.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move and aim up, right, down, or left |
| Space (hold) | Fire whenever your previous projectile has disappeared |
| Enter | Start, restart after game over, or resume when paused |
| P | Pause/resume |
| M | Toggle sound |
| Esc / close window | Exit and record the current game |

Movement is cardinal, not diagonal. When multiple arrows are held, the most
recently pressed held arrow takes priority. Losing focus or minimizing
automatically pauses and clears held keys; press P or Enter to resume.
The window is resizable and preserves the playfield's aspect ratio.

## Rules and scoring

- Defend the eagle base at the bottom of the map.
- Destroy **20 enemy tanks per round**, with up to four active simultaneously.
  Enemies enter through three positions at the top, navigate the maze, and fire.
- You start with three lives, including the current tank. An unshielded hit costs
  one life. A lost tank respawns after a short delay, with a 2.5-second shield.
  If a spawn is occupied, respawning waits or uses another nearby clear position.
- Enemy fire destroying the base, or losing your last life, ends the game.
- Brick tiles block movement and require two shots to destroy. Both teams can
  break walls. Tanks cannot move through one another or the base.
- One projectile per tank may be active; opposing projectiles cancel on contact.
  Friendly fire does not damage the eagle.
- Each enemy awards **100 points**; clearing a round awards **500 bonus points**.
  The next round rebuilds the maze and increases enemy movement speed, up to a cap.
  Lives and score carry over. There are no extra-life or power-up mechanics.

This recreation preserves the core twenty-enemy/base-defense loop, but its maze,
timing, AI, shield, two-hit bricks, scoring and visuals are original design
choices rather than a claim of arcade-accurate emulation.

## Persistent player scores

The HUD shows current score, best score and the last completed/closed game.
The best score, last score and number of recorded games are stored at:

```text
%LOCALAPPDATA%\TankBattalionNative1980\scores.txt
```

A game is recorded once on game over or normal exit, including an unfinished game
closed with Esc. Starting a new game does not double-count the previous game.
The best display updates live; disk persistence happens when a game is recorded.
Forced process termination or power loss may lose the unfinished game's score.
The file is written through a temporary file and replaced with `MoveFileExW`.

Format:

```text
TANK_SCORES 1
1500
900
3
```

The three values are best, last and recorded-game count. Storage or playback
failures produce a Windows warning dialog rather than silently failing.
An invalid score file is not overwritten: close the game, remove the file,
and restart to reset records. Use one game instance at a time; the score store
is not designed for concurrent writers.

## Compile using Visual Studio

### Required components

Install **Desktop development with C++** using Visual Studio Installer.
Include:

- MSVC v145 C++ x64/x86 build tools (Visual Studio 18 / 2026).
- A Windows 10 or Windows 11 SDK.
- MSBuild (included with Visual Studio).

The supplied configuration targets the installed Visual Studio at:

```text
C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe
```

No NuGet packages, game engines, CMake, third-party libraries or downloads are
needed. Dependencies are Windows SDK libraries:
`user32`, `gdi32`, `winmm`, `shell32`, `ole32`, and `uuid`.

### IDE build

1. Open `TankBattalion.sln` from the extracted `tank` folder.
2. Select **Release** (or **Debug**) and **x64** (or **Win32**).
3. Choose **Build > Build Solution** (`Ctrl+Shift+B`).
4. Choose **Debug > Start Without Debugging** (`Ctrl+F5`) to play, or `F5` to debug.

Output: `bin\<platform>\<configuration>\TankBattalion.exe`.
Intermediate files go in `obj\<platform>\<configuration>`.

### Command-line build: supplied script

In a Command Prompt or PowerShell opened in the `tank` folder:

```powershell
.\build.bat
.\build.bat Debug x64
.\build.bat Release Win32
```

The default is `Release x64`. The script uses the installed MSBuild path directly,
so it does not require a developer shell. It reports errors using a nonzero exit
code. If Visual Studio is elsewhere, edit `MSBUILD` inside `build.bat`.

### Command-line build: MSBuild directly

PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  .\TankBattalion.sln /m /p:Configuration=Release /p:Platform=x64
```

Command Prompt:

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" TankBattalion.sln /m /p:Configuration=Release /p:Platform=x64
```

### Command-line build: devenv

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\devenv.exe" TankBattalion.sln /Build "Release|x64"
```

### Compile the source directly (without project files)

Open **x64 Native Tools Command Prompt for VS 2026**, change to `tank`, then:

```bat
if not exist bin\manual mkdir bin\manual
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /DUNICODE /D_UNICODE src\main.cpp /Fo"bin\manual\main.obj" /Fe"bin\manual\TankBattalion.exe" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib shell32.lib ole32.lib uuid.lib
```

Alternatively initialize that environment in a regular Command Prompt first:

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
```

Environment setup and `cl` must run in the same terminal. Use `vcvars32.bat`
instead if compiling a 32-bit executable.

### Other Visual Studio versions

For Visual Studio 2022, open the solution and retarget **Platform Toolset** to
`v143` under Project Properties > Configuration Properties > General
(select All Configurations / All Platforms). Retarget the Windows SDK if prompted.
For other installations update the script paths; the application itself uses
C++17 and public Windows APIs, not Visual Studio-specific game libraries.

### Common build problems

| Error | Resolution |
| --- | --- |
| MSB8020: build tools for v145 not found | Install MSVC v145, or retarget to the installed toolset |
| MSB8036: Windows SDK not found | Install a Windows SDK and retarget the project |
| `cl` not recognized | Use a Native Tools command prompt or call `vcvars64.bat` |
| Cannot open `windows.h` | Install the Windows SDK and C++ desktop workload |
| LNK1104 / executable cannot be written | Close the running game before rebuilding |
| MSBuild path not found | Edit `build.bat` for your installation |

## Verification

The executable has deterministic headless gameplay checks. This mode creates no
window, does not play audio and does not read/write your score file.
In PowerShell (GUI executables should be explicitly awaited):

```powershell
$p = Start-Process .\bin\x64\Release\TankBattalion.exe `
  -ArgumentList "--self-test" -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput self-test.txt
Get-Content .\self-test.txt
if ($p.ExitCode -ne 0) { throw "Gameplay checks failed" }
```

The checks cover starting, movement, wall/base/boundary collision, two-hit bricks,
single player projectiles, enemy kills and score, shield protection, lives,
respawning, projectile cancellation, round transitions and bonus, base loss,
spawn occupancy, corridor turning and enemy limits, plus three simulated minutes
of movement/collision invariants. Success prints the check count and zero failures.

Manual acceptance checks:

1. Start; move in all four directions and hold Space. Confirm visible projectiles,
   wall damage, enemy kills and score changes.
2. Resize repeatedly while moving. The field must remain fully drawn with no
   background erase flashes.
3. Use P, switch to another application, and minimize. Verify simulation pauses,
   held movement stops, and P/Enter resumes.
4. Listen for firing, wall-hit, explosion, round-clear and game-over sounds.
   Toggle M and confirm sound stops and can be restored.
5. Let an enemy destroy the eagle or lose all lives. Confirm the game-over screen.
6. Close after earning points, reopen, and confirm best/last records persist.

## Implementation

`src\main.cpp` contains the gameplay model, renderer, synthesized audio, score
storage, Win32 window procedure and embedded checks. The project compiles with
level-4 warnings treated as errors in Debug and Release, x64 and Win32.

- **Windows APIs:** `RegisterClassExW`, `CreateWindowExW`, the message loop,
  `WM_KEYDOWN`/`WM_KEYUP`, `WM_TIMER`, and `WM_PAINT`.
- **Simulation:** elapsed time from `std::chrono::steady_clock`, fixed 120 Hz steps,
  and a 100 ms catch-up cap to avoid large jumps after stalls. Windows timers
  schedule work; timer delivery is not assumed to be exactly 8 ms.
- **Double buffering:** a persistent compatible bitmap selected into a memory DC.
  Every complete frame is drawn offscreen and presented with a single
  `StretchBlt`. `WM_ERASEBKGND` is suppressed, paint invalidation does not erase,
  and only letterbox margins are cleared directly. No per-frame GDI allocation.
- **Audio:** generated 22,050 Hz, mono, 16-bit PCM WAV data played through
  WinMM `PlaySoundW`. Sound is asynchronous; higher-priority effects replace
  lower-priority ones in a tick. This is intentionally a single-channel effects
  system, not a multi-voice mixer. WAV buffers live until playback is stopped.
- **Cleanup:** GDI resources are deselected/deleted, the timer is removed and sound
  stopped on shutdown. No worker threads or external resources are required.

## Package contents

```text
tank\
  README.md
  build.bat
  TankBattalion.sln
  TankBattalion.vcxproj
  TankBattalion.vcxproj.filters
  src\
    main.cpp
  bin\
    x64\
      Release\
        TankBattalion.exe
```

Debug binaries and intermediate build files are not required to play.
Tank Battalion is a name associated with Namco's original game; this project
is not affiliated with or endorsed by Namco/Bandai Namco.
