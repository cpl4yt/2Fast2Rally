#pragma once

namespace rally::synth {
	// Procedural car audio, generated on the audio thread from a handful of
	// parameters the game updates every frame. No sample files needed:
	// engine (pulse train + harmonics through a load-dependent filter),
	// turbo whistle/blow-off, backfire pops, gear-shift clunk, gravel spray,
	// tarmac squeal, snow crunch, wind, rain and impacts.
	struct CarAudioParams {
		float rpm = 900.0f;
		float idleRpm = 900.0f;
		float redline = 7500.0f;
		int cylinders = 4;
		float throttle = 0.0f;    // 0..1 engine load
		float boost = 0.0f;       // 0..1 turbo spool (0 for N/A cars)
		float speed = 0.0f;       // m/s, for wind/surface intensity
		float gravel = 0.0f;      // 0..1 how much tire is on loose surface (and slipping)
		float tarmacSkid = 0.0f;  // 0..1 squeal
		float snow = 0.0f;        // 0..1
		float grass = 0.0f;       // 0..1
		float rain = 0.0f;        // 0..1
		float engineVolume = 1.0f;
		float effectsVolume = 1.0f;
		bool engineOn = true;
		bool interior = false;    // muffle when in-car
	};

	void init();
	void shutdown();
	void setParams(const CarAudioParams& p);
	void setPaused(bool paused);

	// One-shot triggers.
	void triggerBackfire(int pops = 2);
	void triggerShift();
	void triggerImpact(float strength);  // 0..1
	void triggerBlowOff();
}
