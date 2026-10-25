# 2Fast2Rally — Release TODO

Version 1.1.0: 25 stages in 5 rally events, 9 cars, co-op driver/navigator,
wheels with force feedback. Feature-complete and on the website (early access).
What's left is mostly testing with real people, a few assets, and docs.
The beta roadmap that got it here is in the git history / CHANGELOG.md.

## Needs real people
- [ ] **"Is it fun?"**: drive every event yourself, all cars and surfaces
- [ ] Playtests with 3–5 pairs, using PLAYTEST.md
- [ ] Co-op over the real internet (UDP 27420 forwarded, or Tailscale/ZeroTier)
- [ ] Tune force feedback on real wheels

## Assets
- [ ] Recorded co-driver voice. The current voice is Windows text-to-speech (`tools/genvoice.ps1`), and shipping Microsoft TTS audio in a paid game is a licensing grey area
- [ ] Ship an OFL body font. Bahnschrift is loaded from `C:\Windows\Fonts`, so it's missing on Linux and some Windows editions
- [ ] Real engine recordings, car models, and tarmac/snow textures (drop-in slots in ASSETS.md)
- [ ] Confirm the licences of the reused Prarie assets (trees, rocks, menu music)

## Docs & packaging
- [x] README still says `package.bat` makes `v0.1.0-beta`
- [x] CHANGELOG still says "1.1.0 — in progress" though 1.1.0 is built and on the site: close it out
- [x] Clear the old beta / 1.0.0 zips out of `dist/`

## Website & launcher
- [ ] Re-upload the 1.1.0 build so it gets a SHA-256 hash (it went up before hashing existed; the launcher installs it without a hash to check)
- [ ] Install and play once through the CLKairos Launcher; check `saves/leaderboard.json` survives an update
- [ ] Leave early access when it's ready

## Release (shared checklist)
- [ ] Test the zip on a clean Windows machine (SmartScreen/AV warnings, missing DLLs); consider code signing
- [ ] Performance on low-end / integrated GPUs
- [ ] 1080p, 1440p and ultrawide screens
- [ ] Linux build test
- [ ] Store page: capsule art, screenshots, trailer
- [ ] Repo committed and release tagged

## Later
- Online leaderboards / ghosts / replays
- Matchmaking or a relay, so co-op works without port forwarding
- Career mode, car upgrades, spectators
