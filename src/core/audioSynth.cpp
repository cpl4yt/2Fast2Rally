#include "core/audioSynth.h"
#include <raylib.h>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace rally::synth {
	namespace {
		constexpr int kRate = 44100;
		constexpr float kTau = 6.28318530718f;

		AudioStream stream{};
		bool ready = false;

		std::mutex paramMtx;
		CarAudioParams shared;       // written by game thread
		std::atomic<bool> paused{ false };
		std::atomic<int> backfirePending{ 0 };
		std::atomic<int> shiftPending{ 0 };
		std::atomic<int> blowOffPending{ 0 };
		std::atomic<float> impactPending{ 0.0f };

		// --- audio-thread state ---
		struct Svf {
			// Chamberlin state-variable filter: lp/bp/hp outputs.
			float lp = 0, bp = 0, hp = 0;
			void run(float in, float cutoff, float q) {
				float f = 2.0f * std::sin(3.14159f * std::min(cutoff, kRate * 0.24f) / kRate);
				hp = in - lp - q * bp;
				bp += f * hp;
				lp += f * bp;
				// Keep the filter from blowing up on extreme params.
				if (!std::isfinite(lp) || std::fabs(lp) > 50.0f) { lp = bp = hp = 0; }
			}
		};

		uint32_t rngState = 0x12345678u;
		inline float whiteNoise() {
			rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
			return (rngState & 0xFFFFFF) / float(0x800000) - 1.0f;
		}

		CarAudioParams cur;          // smoothed copy used per-sample
		float enginePhase = 0, subPhase = 0, turboPhase = 0;
		float cycleAmp = 1.0f;
		Svf engineFilter, intakeFilter, gravelFilter, skidFilter, snowFilter, windFilter, rainFilter, impactFilter, popFilter, interiorFilter, clunkFilter, blowFilter;
		float backfireEnv = 0; int backfireLeft = 0; float backfireGap = 0;
		float impactEnv = 0;
		float clunkEnv = 0;
		float blowEnv = 0;
		float gravelClickEnv = 0;
		float dcBlock = 0, dcPrev = 0;

		void smoothParams() {
			CarAudioParams target;
			{
				std::lock_guard<std::mutex> lock(paramMtx);
				target = shared;
			}
			// Per-buffer smoothing (buffers are ~10ms) to avoid zipper noise.
			auto s = [](float& c, float t, float k) { c += (t - c) * k; };
			s(cur.rpm, target.rpm, 0.5f);
			s(cur.throttle, target.throttle, 0.35f);
			s(cur.boost, target.boost, 0.3f);
			s(cur.speed, target.speed, 0.3f);
			s(cur.gravel, target.gravel, 0.3f);
			s(cur.tarmacSkid, target.tarmacSkid, 0.3f);
			s(cur.snow, target.snow, 0.3f);
			s(cur.grass, target.grass, 0.3f);
			s(cur.rain, target.rain, 0.1f);
			cur.idleRpm = target.idleRpm;
			cur.redline = target.redline;
			cur.cylinders = target.cylinders;
			cur.engineVolume = target.engineVolume;
			cur.effectsVolume = target.effectsVolume;
			cur.engineOn = target.engineOn;
			cur.interior = target.interior;
		}

		void callback(void* buffer, unsigned int frames) {
			float* out = static_cast<float*>(buffer);
			if (paused.load()) {
				for (unsigned i = 0; i < frames * 2; ++i) out[i] = 0.0f;
				return;
			}
			smoothParams();

			if (int n = backfirePending.exchange(0)) { backfireLeft += n; }
			if (shiftPending.exchange(0)) clunkEnv = 1.0f;
			if (blowOffPending.exchange(0)) blowEnv = 1.0f;
			float imp = impactPending.exchange(0.0f);
			if (imp > 0.0f) impactEnv = std::max(impactEnv, imp);

			const float dt = 1.0f / kRate;
			float firingHz = cur.rpm / 60.0f * (cur.cylinders * 0.5f);
			float rpmNorm = std::clamp((cur.rpm - cur.idleRpm) / std::max(1.0f, cur.redline - cur.idleRpm), 0.0f, 1.0f);
			float load = cur.throttle;
			float engineGain = cur.engineOn ? (0.32f + 0.38f * load + 0.15f * rpmNorm) * cur.engineVolume : 0.0f;
			float filterCut = 350.0f + 2600.0f * (0.25f + 0.75f * load) * (0.4f + rpmNorm);
			float speed = cur.speed;
			float speedNorm = std::clamp(speed / 50.0f, 0.0f, 1.0f);
			float fx = cur.effectsVolume;

			for (unsigned i = 0; i < frames; ++i) {
				// --- Engine: per-cylinder combustion pulses ---
				enginePhase += firingHz * dt;
				if (enginePhase >= 1.0f) {
					enginePhase -= 1.0f;
					// Small cycle-to-cycle variation keeps it from sounding like a buzzer.
					cycleAmp = 0.82f + 0.18f * (whiteNoise() * 0.5f + 0.5f);
				}
				subPhase += firingHz * 0.5f * dt;
				if (subPhase >= 1.0f) subPhase -= 1.0f;
				float pulse = std::exp(-enginePhase * (5.0f + 6.0f * (1.0f - load))) * cycleAmp;
				float harmonics = 0.55f * std::sin(kTau * enginePhase) + 0.25f * std::sin(kTau * 2.0f * enginePhase + 0.7f)
					+ 0.35f * std::sin(kTau * subPhase);
				float raw = pulse * 0.9f + harmonics * 0.5f + whiteNoise() * 0.08f * (0.3f + load);
				engineFilter.run(raw, filterCut, 0.7f);
				float engine = engineFilter.lp;

				// Intake/exhaust roar: noise band that tracks rpm.
				intakeFilter.run(whiteNoise(), 300.0f + firingHz * 3.0f, 0.5f);
				engine += intakeFilter.bp * 0.25f * load * (0.3f + rpmNorm);

				// Turbo whistle.
				turboPhase += (1800.0f + 3200.0f * cur.boost) * dt;
				if (turboPhase >= 1.0f) turboPhase -= 1.0f;
				engine += std::sin(kTau * turboPhase) * 0.025f * cur.boost * cur.boost;

				float sample = engine * engineGain;

				// --- Backfire pops (anti-lag/overrun crackle) ---
				if (backfireLeft > 0 && backfireEnv < 0.05f) {
					backfireGap -= dt;
					if (backfireGap <= 0.0f) {
						backfireEnv = 1.0f;
						--backfireLeft;
						backfireGap = 0.04f + 0.08f * (whiteNoise() * 0.5f + 0.5f);
					}
				}
				if (backfireEnv > 0.001f) {
					popFilter.run(whiteNoise(), 900.0f, 0.4f);
					sample += popFilter.lp * backfireEnv * 1.6f * cur.engineVolume;
					backfireEnv *= 0.9985f;
				}

				// --- Blow-off valve ---
				if (blowEnv > 0.001f) {
					blowFilter.run(whiteNoise(), 2500.0f + 2000.0f * blowEnv, 0.3f);
					sample += blowFilter.bp * blowEnv * 0.18f * cur.engineVolume;
					blowEnv *= 0.99985f;
				}

				// --- Shift clunk ---
				if (clunkEnv > 0.001f) {
					clunkFilter.run(whiteNoise(), 180.0f, 0.3f);
					sample += clunkFilter.lp * clunkEnv * 1.2f * fx;
					clunkEnv *= 0.9992f;
				}

				// --- Surfaces ---
				if (cur.gravel > 0.01f) {
					// Sparse stone clicks over a rumble bed.
					if (whiteNoise() > 1.0f - 0.004f * cur.gravel * (0.3f + speedNorm * 2.0f)) gravelClickEnv = 0.7f + 0.3f * whiteNoise();
					gravelFilter.run(whiteNoise(), 1600.0f, 0.6f);
					sample += (gravelFilter.bp * 0.35f + whiteNoise() * gravelClickEnv * 0.5f) * cur.gravel * (0.2f + speedNorm) * fx;
					gravelClickEnv *= 0.97f;
				}
				if (cur.tarmacSkid > 0.01f) {
					skidFilter.run(whiteNoise(), 950.0f + 150.0f * std::sin(kTau * 3.0f * i * dt), 0.08f);
					sample += skidFilter.bp * 0.22f * cur.tarmacSkid * fx;
				}
				if (cur.snow > 0.01f || cur.grass > 0.01f) {
					snowFilter.run(whiteNoise(), 500.0f, 0.5f);
					sample += snowFilter.lp * (cur.snow + cur.grass * 0.6f) * 0.5f * (0.2f + speedNorm) * fx;
				}

				// --- Wind & rain ---
				windFilter.run(whiteNoise(), 300.0f + speed * 15.0f, 0.6f);
				sample += windFilter.lp * speedNorm * speedNorm * 0.35f * fx;
				if (cur.rain > 0.01f) {
					rainFilter.run(whiteNoise(), 5000.0f, 0.9f);
					sample += rainFilter.hp * cur.rain * 0.12f * fx;
				}

				// --- Impacts ---
				if (impactEnv > 0.001f) {
					impactFilter.run(whiteNoise(), 300.0f + 1500.0f * impactEnv, 0.4f);
					sample += impactFilter.lp * impactEnv * 2.2f * fx;
					impactEnv *= 0.99965f;
				}

				// In-car: muffle highs.
				if (cur.interior) {
					interiorFilter.run(sample, 1800.0f, 0.9f);
					sample = interiorFilter.lp * 1.15f;
				}

				// DC block + soft clip.
				float y = sample - dcPrev + 0.995f * dcBlock;
				dcPrev = sample; dcBlock = y;
				y = std::tanh(y * 0.9f);
				out[i * 2] = y;
				out[i * 2 + 1] = y;
			}
		}
	}

	void init() {
		if (ready || !IsAudioDeviceReady()) return;
		SetAudioStreamBufferSizeDefault(1024);
		stream = LoadAudioStream(kRate, 32, 2);
		SetAudioStreamCallback(stream, callback);
		PlayAudioStream(stream);
		ready = true;
	}

	void shutdown() {
		if (!ready) return;
		StopAudioStream(stream);
		UnloadAudioStream(stream);
		ready = false;
	}

	void setParams(const CarAudioParams& p) {
		std::lock_guard<std::mutex> lock(paramMtx);
		shared = p;
	}

	void setPaused(bool p) { paused = p; }
	void triggerBackfire(int pops) { backfirePending += pops; }
	void triggerShift() { shiftPending = 1; }
	void triggerImpact(float strength) {
		float s = std::clamp(strength, 0.0f, 1.0f);
		float prev = impactPending.load();
		if (s > prev) impactPending = s;
	}
	void triggerBlowOff() { blowOffPending = 1; }
}
