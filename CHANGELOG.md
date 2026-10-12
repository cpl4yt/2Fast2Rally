# Changelog

## 1.1.0 — in progress

**New**
- **Drop-in asset slots** (see `ASSETS.md`): recorded co-driver voice, engine packs
  (RPM loops crossfaded and pitch-shifted, on/off throttle), surface/wind/rain loops,
  impact/glass/shift/stone one-shots, menu sounds, stage cues, ambience + crowd, a music
  playlist and photo textures for tarmac/snow/mud. Anything missing keeps the built-in sound/texture.
- **Tyre marks**: black skid marks on tarmac when sliding, ruts in gravel, snow, mud and grass.
- **In-car head movement**: the camera leans with braking, acceleration and cornering,
  shakes on rough gravel and jolts on impacts (*In-car head movement* slider, Settings -> Graphics).
- Stages fade in from black.
- Cars can share an engine recording via `"sound"` in `cars.json`.
- `--audiodump` (with `--selftest`): records the car-audio mix to `logs/<shots>_audio.wav`.

## 1.0.0 — first release

**New**
- **Realistic drivetrain**: engine, clutch and gearbox are simulated with torque -
  the throttle only revs the engine and drive goes through the clutch. Three
  transmissions: Automatic, Sequential (Q/E) and **H-pattern + clutch**, where you can
  slip the clutch, stall (restart with Ignition, I) and grind gears without the clutch.
- **Wheels, pedals and H-shifters** read directly through DirectInput (up to 4 devices):
  bind steering, throttle, brake, **clutch** axes and gears 1-6/R/N to any wheel or
  shifter button in Settings -> Controls.
- New logo (main menu, window and exe icon).
- **Instrument cluster**: analog tachometer (redline, shift lights, gear window),
  speedometer, turbo boost gauge, pedal bars and warning lamps replace the HUD box.
- **25 stages in 5 rally events**: Season Opener (mixed), Rally Kallio (fast forest gravel),
  Rally Brume (mountain tarmac, incl. snow/ice patches), Rally Frostvik (snow) and
  Rally Ridgeback (mountain gravel, mixed surfaces, mud). Stages run 1.8-4 km.
- **Rally mode**: pick an event and drive its stages back to back on a combined time,
  solo or in co-op. Each event has its own leaderboard. Editor-made stages go into
  a Custom Stages event.
- **9 cars**: new Vespera (FWD hot hatch), Imperia (AWD turbo saloon), Quattra
  (Group B 5-cylinder), Mito (classic mini), Vortex (modern Rally1), Escala (70s RWD)
  and Banshee (V8 rally-raid 4x4), each with its own body style and livery.
- **Rev-matched shifts**: with the H-pattern you can shift without the clutch if you
  match the revs, like in a real car.
- **Cockpit**: three-spoke wheel, gauges on the dash behind it, seats, roll cage,
  hydraulic handbrake and gear lever; *Seat height* / *Seat forward* sliders in
  Settings -> Graphics.
- **Night** time of day: stars, moonlight, and you drive on your headlights and light pod.
- **1:1 steering wheels**: a wheel bound through DirectInput steers raw (no smoothing,
  curve, deadzone or speed assist); set *Wheel rotation* to your wheel's range and the
  in-car wheel and front wheels follow it exactly.
- **Steering-wheel force feedback** (DirectInput): self-aligning torque from the
  front tyres, speed-sensitive centring, gravel/snow road texture, impact kicks.
  Strength slider + invert toggle.
- **Gamepad rumble** (XInput) for slides, surfaces and impacts.
- **Traction control** option (on by default) and ramped keyboard pedals.
- **How to Play** screen (shown on first launch) and **Credits** screen.
- Volume sliders in the pause menu.
- Windows exe icon and version info.

**Changed**
- Terrain generation rebuilt: the land follows the road smoothly and fades into
  rolling hills — no more cliffs or walls next to the road.
- Stage generator never lets the road come back within 70 m of itself
  (no overlapping or crossing roads); hairpin sequences removed. The stage editor
  warns if an edited road gets too close to itself.
- Default camera is the cockpit view (it's a first-person rally game).
- Network protocol version 2 — 1.0 and the beta can't join each other.
- **Hitboxes reworked**: trees, rocks and logs collide at their real (measured) size;
  the car's roof, pillars, nose and sides touch the ground, not just its corners.
- Damage resets when a stage ends or restarts — every stage starts with a fresh car.
- Car models polished: shut lines, handles, skirts, plates, aerials, lamp housings,
  quarter glass and a proper interior you can see through the windows.

**Fixed**
- One shift press could skip gears (1st -> 3rd -> 6th); the automatic box could cascade
  up through the gears at launch.
- Pace-note arrows on the HUD pointed the wrong way.
- The text-to-speech co-driver no longer reads notes when a human is navigating.
- Esc opened and immediately closed the pause menu.
- Cockpit view: the dash and roof lining no longer block the road.
- Solo rally: "Next Stage" could load a stage from the wrong event.
- The in-car steering wheel turned the wrong way.
- Door numbers mirrored on one side of the car.
- Roadside posts/rocks could end up on the road; verge strips drawn across the road.
- Dust and spray glowed at dusk/night.
- AI driver: reverses out of trouble, safer corner speeds.
- Damage is far less sensitive: landings no longer count, small knocks do nothing,
  and a wrecked engine loses 35% power (was 45%).

## 0.1.0-beta
- First playable: co-op driver/navigator, 3 stages, 2 cars, pace notes, lobby,
  leaderboards, stage editor.
