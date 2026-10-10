# 2Fast2Rally — Beta Roadmap

Beta goal: two players (driver + navigator) can join over the network, run 3 full stages
on 2 surfaces with pace notes, get a time, and see it on a results screen.

Status (v0.1.0-beta): **done**, apart from the items that need real people or a
second internet connection (marked ⏳). Verified with automated runs: the AI driver
completes all 3 stages, and a host + navigator pair on localhost finish together
(including a mid-stage disconnect and rejoin).

---

## Phase 0 — Project setup (reuse Prairie)
- [x] Copy CMake setup + `lib/` (raylib 6.0, enet, nlohmann json) from Prarie — sol2/lua dropped (tuning uses JSON)
- [x] Create `src/core/` and pull engine layers out of Prairie:
  - [x] `physicsEngine` (+ `applyImpulseAtPoint` for wheel forces)
  - [x] `networkManager` (+ ENet), message set replaced with rally messages
  - [x] Terrain — new spline-aware heightfield in `stage.cpp` (Prairie's chunked terrain is tied to its world gen)
  - [x] Audio — new procedural synth (`audioSynth`) instead of `audioManager` (no engine samples needed)
  - [x] Controls/settings — new `input` + `settings` with full rebinding, gamepad & wheel axes
  - [x] Models/textures from Prairie (trees, rocks, grass, gravel); `assetPack` not needed (ships `res/`)
  - [x] UI kit (`ui`) and debug overlay (F3)
  - [x] Lit shader with fog, wetness and headlights
- [x] Namespace `rally`
- [x] No survival code carried over
- [x] `main.cpp`, game loop, fixed 120 Hz physics, interpolated rendering
- [x] Builds cleanly on MinGW (static, no DLLs); `.gitignore`

## Phase 1 — Driving feel
- [x] `Vehicle`: chassis = `PhysicsEngine::Body`, 4 wheels
- [x] Raycast suspension (spring, damper, bump stop, travel)
- [x] Tire model: slip-angle lateral curve, friction circle, load-dependent grip
- [x] Engine torque curve, turbo spool, rev limiter, auto + manual gearbox, reverse
- [x] AWD (Kestrel), RWD (Bianca); FWD supported in `cars.json`
- [x] Speed-sensitive steering; handbrake locks/disconnects the rear
- [x] Brakes + ABS toggle; traction control toggle
- [x] Surfaces: tarmac, gravel, snow, mud, grass, dirt
- [x] Anti-roll bars, anti-flip assist, reset to road (R)
- [x] Gamepad/wheel analog input + keyboard (ramped pedals)
- [x] Chase, near chase, bonnet, cockpit cameras (+ look back)
- [x] Debug overlay: per-wheel load/slip/surface, RPM, gear, G
- [x] Tuning in `res/data/cars.json`, hot reload with F5
- [ ] ⏳ **Milestone: "is it fun?"** — needs you behind the wheel

## Phase 2 — Stages
- [x] Stage JSON: control points, width, surface segments, bridges, jumps, note overrides
- [x] Road mesh from spline, blended into terrain (cuttings, ditches, snowbanks)
- [x] Start, 2 splits, finish (gantries/boards)
- [x] Off-course detection + reset (+5 s), roll-over reset, corner-cut penalty (+2 s)
- [x] Props: trees, rocks, marker posts, spectators, don't-cut rocks (Prairie models)
- [x] In-game stage editor (drag/add/delete points, surfaces, bridges, jumps, note overrides, test drive)
- [x] 3 stages:
  - [x] Kallio Forest — gravel, ~3 km, bridge, 3 jumps, mud section
  - [x] Col de Brume — tarmac mountain, ~3 km, hairpins
  - [x] Frostvik Snow — ~2 km, snowbanks

## Phase 3 — Pace notes
- [x] Generated from curvature: 1–6, hairpin, square, L/R, link distances (into/and/50…300)
- [x] Modifiers: long, tightens, opens, don't cut, caution, over crest; crest/jump/bridge/finish notes
- [x] Manual overrides (editor + JSON)
- [x] Navigator timeline with distances and "wait / CALL NOW / LATE!" hint
- [x] Navigator call input (key / pad / click a note)
- [x] Driver HUD shows only called notes
- [x] Voice clips for every word (Windows TTS via `tools/genvoice.ps1` — swap in real recordings any time)
- [x] Voice can be turned off to use real voice chat
- [x] Call-timing feedback (on time / early / late / missed + score)

## Phase 4 — Co-op networking
- [x] Host / join by IP + port, lobby
- [x] Messages: Hello, Lobby, Ready, StartStage, CarState, NoteCall, StageEvent, Pause, BackToLobby
- [x] Driver authoritative, car state at 30 Hz unreliable
- [x] Navigator interpolation (100 ms buffer)
- [x] Reliable note calls + stage events
- [x] Ready-up + synced countdown
- [x] Disconnect pauses the stage; rejoin resumes it (tested); driver can continue with AI co-driver
- [x] Role swap (lobby and results screen)
- [ ] ⏳ Test over the real internet (needs a second location — see README: forward UDP 27420 or use a VPN)

## Phase 5 — Navigator experience
- [x] Views: co-driver seat, chase, stage map, pace-note book (Tab)
- [x] Stage map + minimap with car position
- [x] Split times + deltas shown to the navigator only (in co-op)
- [x] Co-driver in the car (procedural, holding the notebook)

## Phase 6 — Game loop & UI
- [x] Main menu
- [x] Lobby: roles, stage, car, weather, time of day, ready
- [x] Loading → countdown → run → finish → results
- [x] Results: time, splits, penalties, resets, cuts, call stats
- [x] Local leaderboard per stage (`saves/leaderboard.json`)
- [x] Settings: graphics, audio, controls rebinding, gameplay (km/h vs mph, gearbox, ABS, TCS, voice)
- [x] Pause menu, synced in co-op

## Phase 7 — Content & polish
- [x] 2 cars: Kestrel 4WD Turbo (AWD), Bianca 2.4 (RWD) — procedural low-poly models with liveries
- [x] Engine audio: RPM-tracked synth, turbo whistle, blow-off, backfires, shift clunk
- [x] Surface audio: gravel, tarmac squeal, snow, grass; wind; rain; impacts
- [x] Particles: dust, gravel stones, snow spray, tyre smoke, exhaust, backfire flames, rain, snowfall
- [x] Visual damage: dirt build-up, broken headlights/pod
- [x] Mechanical damage: engine power, steering pull, suspension
- [x] Weather: clear / overcast / rain (wet grip)
- [x] Time of day: day + dusk (headlights + light pod)

## Phase 8 — Beta release
- [x] Performance: ~220 FPS average on this PC with 3 instances running (Intel HD 620 not available to test)
- [x] Crash logging (`logs/crash-*.txt` with stack trace) + `logs/rally.log`
- [x] Windows build packaged: `package.bat` → `dist/2Fast2Rally-v0.1.0-beta-win64.zip`
- [x] README: host/join, controls, tuning, editor
- [x] Playtest feedback form: `PLAYTEST.md`
- [ ] ⏳ Playtest with 3–5 duos — needs real players
- [x] Bug bash (automated runs on every stage/car/weather; fixes for hairpin props, stop zone, cockpit, RWD traction)
- [x] Tag `v0.1.0-beta`

## v1.0.0 (done)
- [x] Rally mode (solo + co-op), 5 stages, 3 cars (AWD/RWD/FWD), night
- [x] Steering-wheel force feedback + gamepad rumble
- [x] Smoother terrain, no self-overlapping roads, editor overlap warning
- [x] How to Play / Credits, exe icon + version info, pause-menu fixes

## Still needs people
- [ ] Playtests with 3-5 pairs (PLAYTEST.md)
- [ ] Real internet co-op test
- [ ] Tune force feedback on real wheels

---

## Out of scope for beta (later)
- Steam / matchmaking / relay servers
- Online leaderboards
- Career mode, car upgrades
- Replays / ghosts
- Linux build (NixOS) — CMake already supports it, untested
- More than 2 players / spectators
