# Changelog

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
- **Rally mode**: run every stage back to back with a combined time, solo or in co-op.
  Full-rally times get their own leaderboard.
- **2 new stages**: Ridgeback Gravel (mountain gravel) and Lakeside Sprint
  (mixed tarmac/gravel) — 5 stages in total.
- **New car**: Vespera 1.6 FWD hot hatch — 3 cars (AWD, RWD, FWD).
- **Night** time of day: stars, moonlight, and you drive on your headlights and light pod.
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

**Fixed**
- One shift press could skip gears (1st -> 3rd -> 6th); the automatic box could cascade
  up through the gears at launch.
- Pace-note arrows on the HUD pointed the wrong way.
- The text-to-speech co-driver no longer reads notes when a human is navigating.
- Esc opened and immediately closed the pause menu.
- The in-car steering wheel turned the wrong way.
- Door numbers mirrored on one side of the car.
- Roadside posts/rocks could end up on the road; verge strips drawn across the road.
- Dust and spray glowed at dusk/night.
- AI driver: reverses out of trouble, safer corner speeds.

## 0.1.0-beta
- First playable: co-op driver/navigator, 3 stages, 2 cars, pace notes, lobby,
  leaderboards, stage editor.
