#pragma once
#include <string>

namespace rally::synth {
	// Car audio, mixed on the audio thread from a handful of parameters the
	// game updates every frame. Every layer can come from recordings (see
	// ASSETS.md); anything without a recording is synthesised instead:
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
		float tarmac = 0.0f;      // 0..1 share of tyres rolling on tarmac
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

	// Recordings. Call from the main thread; safe while audio is playing.
	// Engine pack folder: on_<rpm>.wav / off_<rpm>.wav loops (+ idle.wav,
	// turbo.wav, and one-shot variants like backfire_1.wav). "" clears it.
	void loadEnginePack(const std::string& dir);
	// Shared effects folder (surface loops, impacts, shifts, ...).
	void loadEffects(const std::string& dir);
	// True if a one-shot group (e.g. "glass") has at least one recording.
	bool hasEffect(const std::string& group);

	// Debug: record everything the mixer outputs (mono) and write it as a WAV.
	void startCapture();
	void stopCapture(const std::string& wavPath);

	// One-shot triggers.
	void triggerBackfire(int pops = 2);
	void triggerShift();
	void triggerImpact(float strength);  // 0..1
	void triggerBlowOff();
	// Plays a recorded one-shot group if there is one (random variant,
	// slight pitch variation). Silently ignored otherwise.
	void playEffect(const std::string& group, float gain = 1.0f);
}
