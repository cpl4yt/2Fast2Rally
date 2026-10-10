# 2Fast2Rally
First person rally co-op.

A co-op rally game: **one player drives, the other navigates.** The navigator
has the pace notes and the stage map; the driver only sees what the navigator
calls. Built in C++ on raylib, reusing the engine pieces from Prairie
(rigid-body physics, ENet networking, build setup, tree/rock models).

**Version 0.1.0-beta** - 3 stages, 2 cars, gravel / tarmac / snow, rain and dusk.

---

## Playing

### Solo
Main menu -> **Solo Stage**. Pick a stage, car, weather and time of day, and a role:
- **Driver** - an AI co-driver reads the pace notes to you.
- **Navigator** - an AI drives; you practise calling the notes on time.

### Co-op (2 players)
1. One player picks **Host Co-op** and creates a lobby. The screen lists the
   IP addresses to give to your partner (UDP port **27420** by default).
2. The other picks **Join Co-op**, types that address and joins.
3. The host chooses the stage, car, weather and who drives (**Swap Driver /
   Navigator**). Both press **I'm ready**, then the host presses **START STAGE**.

Playing over the internet: the host forwards **UDP 27420** on their router, or
both players join the same VPN (Tailscale, ZeroTier, WireGuard) and use the
VPN address. Windows may ask to allow 2Fast2Rally through the firewall the
first time you host - allow it on private networks.

If the connection drops mid-stage the game pauses. The navigator can press
**Reconnect**; the driver can keep waiting or **continue with the AI co-driver**.

### Controls (rebind everything in Settings -> Controls)

| Driver | Keyboard | Gamepad |
|---|---|---|
| Throttle / Brake & reverse | W / S (or arrows) | RT / LT |
| Steer | A / D (or arrows) | Left stick |
| Handbrake | Space | X / Square |
| Shift up / down (manual box) | E / Q | RB / LB |
| Reset to road (+5 s) | R | Y / Triangle |
| Camera (chase, near, bonnet, cockpit) | C | B / Circle |
| Look back | B | Right stick click |
| Pause | Esc / P | Start |

| Navigator | Keyboard | Gamepad |
|---|---|---|
| Call the next note | Space / Enter (or click a note) | A / Cross |
| Change view (seat, chase, map, pace book) | Tab | B / Circle |

Steering wheels: Settings -> Controls -> *Steering / Throttle / Brake axis*,
click and move the wheel or pedal. Use *Invert* / *Pedal* if a pedal reads
backwards.

Other keys: **F11** fullscreen, **F3** debug overlay, **F5** reload car tuning.

### Pace notes
Corners are graded **1** (slowest) to **6** (fastest), or **hairpin** / **square**.
Modifiers: *long, tightens, opens, don't cut, caution, over crest*. The link
after a note is the distance to the next one (*into* = straight into it,
*and* = very close, *50 / 70 / 100...* = metres).

The navigator's list shows the distance to each note and a timing hint
(**wait / CALL NOW / LATE!**). After the stage you get a call-timing score:
on time, early, late or missed.

### Rules
- **Reset** (R, rolling over, or going far off course): +5 s.
- **Cutting a corner** (rejoining much further along than you drove): +2 s.
- Split times and deltas against your best go to the navigator, who tells the driver.
- Times are saved per stage in `saves/leaderboard.json` (top 10).

---

## Building

Windows (MinGW, the same toolchain as Prairie):
```bat
build.bat            :: or "build.bat console" to keep a log console
```
Needs `C:\w64devkit` (GCC) and CMake (`C:\cmake` or on PATH). The output is
`build\2Fast2Rally.exe`, with `res\` copied next to it.

Release zip: `package.bat` produces `dist\2Fast2Rally-v0.1.0-beta-win64.zip`.

Linux/macOS: `cmake -S . -B build && cmake --build build` (raylib is fetched
automatically if it isn't installed).

## Modding & tuning
- **Cars**: `res/data/cars.json` holds the engine torque curve, gearing,
  suspension, tyre grip, brakes, steering and livery. Edit it while driving and
  press **F5** to reload.
- **Stages**: `res/data/stages/*.json` (control points, surface segments,
  bridges, jumps, pace-note overrides). Edit them in-game with
  **Stage Editor** (drag points, Shift+click to add, 1-6 surface, B bridge,
  J jump, Ctrl+S to save, Test Drive).
- **Co-driver voice**: `tools/genvoice.ps1` regenerates `res/audio/notes/*.wav`
  with Windows text-to-speech. Drop in your own recordings with the same names.

## Testing hooks
```
2Fast2Rally.exe --selftest --stage 0 --car 0 [--weather 0-2] [--time 0-1] [--navigator] --shots NAME
2Fast2Rally.exe --nettest-host --stage 2 --shots host     (then, separately)
2Fast2Rally.exe --nettest-join --shots join
2Fast2Rally.exe --menushot --shots menu
```
The AI drives a full stage, writes `logs/NAME.log` (progress, speed, FPS) and
screenshots, then exits. Crashes write `logs/crash-*.txt` with a stack trace.

## Code layout
```
src/core/   physicsEngine (from Prairie), networkManager (from Prairie), audioSynth,
            input, settings, ui, log, crashHandler, noise
src/game/   vehicle (raycast suspension + tyre model), stage (spline road, terrain,
            props), stageGen, paceNotes, voice, world (renderer), carRender,
            particles, session (co-op protocol), leaderboard, game* (screens), editor
res/        models & textures (from Prairie), stages, cars, voice clips, fonts
```

## Known beta limitations
- The co-driver voice is text-to-speech, not a recorded co-driver.
- Online play is direct IP only: no matchmaking or relay server yet.
- The AI driver is cautious and sometimes needs a reset on tight hairpins.
- Car models are procedural low-poly.
