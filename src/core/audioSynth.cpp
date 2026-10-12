#include "core/audioSynth.h"
#include "core/log.h"
#include <raylib.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

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

		// --- Recordings ---
		// Mono float clips at kRate. Loops are made seamless at load time.
		struct Clip { std::vector<float> s; float rpm = 0; };
		struct Bank {
			std::vector<Clip> on, off;                          // engine loops, sorted by rpm
			std::map<std::string, Clip> loops;                  // "gravel", "wind", "turbo", ...
			std::map<std::string, std::vector<Clip>> shots;     // one-shot groups with variants
		};
		std::mutex bankMtx;
		std::shared_ptr<const Bank> engineBank, fxBank;
		std::atomic<int> bankGen{ 0 };
		struct ShotReq { std::string group; float gain; };
		std::mutex shotMtx;
		std::vector<ShotReq> shotQueue;
		std::atomic<bool> capturing{ false };
		std::mutex captureMtx;
		std::vector<float> captureBuf;

		bool loadClip(const std::filesystem::path& path, Clip& c) {
			Wave w = LoadWave(path.string().c_str());
			if (w.data == nullptr || w.frameCount == 0) return false;
			WaveFormat(&w, kRate, 32, 1);
			float* f = LoadWaveSamples(w);
			c.s.assign(f, f + w.frameCount);
			UnloadWaveSamples(f);
			UnloadWave(w);
			return c.s.size() > 64;
		}

		// Crossfades the tail into the head so the clip loops without a click.
		void makeLoopable(Clip& c) {
			size_t n = c.s.size();
			size_t L = std::min(n / 4, (size_t)(kRate * 0.08f));
			if (L < 16) return;
			for (size_t i = 0; i < L; ++i) {
				float t = (float)i / L;
				c.s[i] = c.s[i] * std::sqrt(t) + c.s[n - L + i] * std::sqrt(1.0f - t);
			}
			c.s.resize(n - L);
		}

		float rms(const Clip& c) {
			double acc = 0;
			for (float v : c.s) acc += (double)v * v;
			return (float)std::sqrt(acc / std::max<size_t>(1, c.s.size()));
		}
		void scaleClip(Clip& c, float k) { for (float& v : c.s) v *= k; }
		void normalizePeak(Clip& c, float target) {
			float pk = 0;
			for (float v : c.s) pk = std::max(pk, std::fabs(v));
			if (pk > 1e-4f) scaleClip(c, target / pk);
		}

		// "impact_light_2" -> "impact_light"; "gravel_loop" -> "gravel" (loop = true).
		std::string groupName(std::string stem, bool& loop) {
			std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
			loop = false;
			size_t us = stem.find_last_of('_');
			if (us != std::string::npos && us + 1 < stem.size() && std::all_of(stem.begin() + us + 1, stem.end(), ::isdigit)) stem.resize(us);
			if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_loop") == 0) { loop = true; stem.resize(stem.size() - 5); }
			return stem;
		}

		std::shared_ptr<Bank> loadBank(const std::string& dir, bool enginePack) {
			auto bank = std::make_shared<Bank>();
			std::error_code ec;
			if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return bank;
			for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
				std::string ext = e.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
				if (ext != ".wav" && ext != ".ogg" && ext != ".mp3" && ext != ".flac") continue;
				std::string stem = e.path().stem().string();
				std::string lower = stem;
				std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
				Clip c;
				if (!loadClip(e.path(), c)) { log::warn("audio: could not load %s", e.path().string().c_str()); continue; }
				if (enginePack && (lower.rfind("on_", 0) == 0 || lower.rfind("off_", 0) == 0)) {
					bool on = lower[1] == 'n';
					c.rpm = (float)std::atof(lower.c_str() + (on ? 3 : 4));
					if (c.rpm < 300.0f) { log::warn("audio: %s has no rpm in its name", stem.c_str()); continue; }
					makeLoopable(c);
					(on ? bank->on : bank->off).push_back(std::move(c));
					continue;
				}
				bool loop = false;
				std::string g = groupName(stem, loop);
				if (enginePack && g == "turbo") loop = true;
				if (loop) { makeLoopable(c); float r = rms(c); if (r > 1e-4f) scaleClip(c, 0.2f / r); bank->loops[g] = std::move(c); }
				else { normalizePeak(c, 0.9f); bank->shots[g].push_back(std::move(c)); }
			}
			// Engine loops keep their relative loudness; the pack as a whole is levelled.
			auto bySpeed = [](const Clip& a, const Clip& b) { return a.rpm < b.rpm; };
			std::sort(bank->on.begin(), bank->on.end(), bySpeed);
			std::sort(bank->off.begin(), bank->off.end(), bySpeed);
			if (bank->on.size() > 16) bank->on.resize(16);
			if (bank->off.size() > 16) bank->off.resize(16);
			float sum = 0; int n = 0;
			for (auto* set : { &bank->on, &bank->off }) for (auto& c : *set) { sum += rms(c); ++n; }
			if (n > 0 && sum > 1e-4f) {
				float k = std::clamp(0.25f / (sum / n), 0.05f, 40.0f);
				for (auto* set : { &bank->on, &bank->off }) for (auto& c : *set) scaleClip(c, k);
			}
			return bank;
		}

		void logBank(const char* what, const std::string& dir, const Bank& b) {
			int shots = 0;
			for (auto& kv : b.shots) shots += (int)kv.second.size();
			if (b.on.empty() && b.off.empty() && b.loops.empty() && shots == 0) return;
			log::info("audio: %s %s: %d on / %d off engine loops, %d loops, %d one-shots",
				what, dir.c_str(), (int)b.on.size(), (int)b.off.size(), (int)b.loops.size(), shots);
		}

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
			s(cur.tarmac, target.tarmac, 0.3f);
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

		// --- Recording playback (audio thread only) ---
		enum LoopId { LGravel, LTarmac, LSkid, LSnow, LGrass, LWind, LRain, LTurbo, LCount };
		const char* const kLoopNames[LCount] = { "gravel", "tarmac", "skid", "snow", "grass", "wind", "rain", "turbo" };
		struct Play { const Clip* c = nullptr; double pos = 0; float pitch = 1, gain = 1; };
		std::array<Play, 16> plays;
		std::array<double, 16> posOn{}, posOff{};
		std::array<double, LCount> loopPos{};
		int seenGen = -1;
		std::vector<ShotReq> shotWork;
		float offLp = 0, engineFade = 1;

		// Linear-interpolated read that wraps (loops).
		inline float tap(const Clip& c, double& pos, float pitch) {
			size_t n = c.s.size();
			size_t i = (size_t)pos;
			float f = (float)(pos - (double)i);
			float a = c.s[i % n], b = c.s[(i + 1) % n];
			pos += pitch;
			if (pos >= (double)n) pos = std::fmod(pos, (double)n);
			return a + (b - a) * f;
		}

		// The two engine loops either side of rpm, equal-power crossfaded.
		struct Pair { int a = -1, b = -1; float ga = 0, gb = 0, pa = 1, pb = 1; };
		Pair pickPair(const std::vector<Clip>& set, float rpm) {
			Pair p;
			int n = (int)set.size();
			if (n == 0) return p;
			auto pitch = [&](int i) { return std::clamp(rpm / set[i].rpm, 0.3f, 3.0f); };
			if (n == 1 || rpm <= set[0].rpm) { p.a = 0; p.ga = 1; p.pa = pitch(0); return p; }
			if (rpm >= set[n - 1].rpm) { p.a = n - 1; p.ga = 1; p.pa = pitch(n - 1); return p; }
			int i = 0;
			while (i + 2 < n && set[i + 1].rpm <= rpm) ++i;
			float t = (rpm - set[i].rpm) / std::max(1.0f, set[i + 1].rpm - set[i].rpm);
			p.a = i; p.b = i + 1;
			p.ga = std::cos(t * 1.5707963f); p.gb = std::sin(t * 1.5707963f);
			p.pa = pitch(i); p.pb = pitch(i + 1);
			return p;
		}
		float playPair(const std::vector<Clip>& set, const Pair& p, std::array<double, 16>& pos) {
			float v = 0;
			if (p.a >= 0) v += tap(set[p.a], pos[p.a], p.pa) * p.ga;
			if (p.b >= 0) v += tap(set[p.b], pos[p.b], p.pb) * p.gb;
			return v;
		}

		const std::vector<Clip>* findShots(const Bank* eng, const Bank* fx, const std::string& g) {
			for (const Bank* b : { eng, fx }) {
				if (!b) continue;
				auto it = b->shots.find(g);
				if (it != b->shots.end() && !it->second.empty()) return &it->second;
			}
			return nullptr;
		}
		void startShot(const std::vector<Clip>& v, float gain) {
			whiteNoise();
			const Clip& c = v[(rngState >> 8) % v.size()];
			Play* slot = &plays[0];
			for (Play& p : plays) {
				if (!p.c) { slot = &p; break; }
				if (p.pos / p.c->s.size() > slot->pos / slot->c->s.size()) slot = &p; // steal the most-finished
			}
			slot->c = &c;
			slot->pos = 0;
			slot->pitch = 1.0f + whiteNoise() * 0.04f;
			slot->gain = gain;
		}

		void callback(void* buffer, unsigned int frames) {
			float* out = static_cast<float*>(buffer);
			if (paused.load()) {
				for (unsigned i = 0; i < frames * 2; ++i) out[i] = 0.0f;
				return;
			}
			smoothParams();

			// Snapshot the recordings for this buffer; a reload resets playback.
			std::shared_ptr<const Bank> eng, fxb;
			{
				std::lock_guard<std::mutex> lock(bankMtx);
				eng = engineBank; fxb = fxBank;
			}
			int gen = bankGen.load();
			if (gen != seenGen) {
				seenGen = gen;
				for (Play& p : plays) p.c = nullptr;
				posOn.fill(0); posOff.fill(0); loopPos.fill(0);
			}
			const Bank* E = eng.get();
			const Bank* F = fxb.get();
			const float fx = cur.effectsVolume;
			auto isEngineGroup = [](const std::string& g) { return g == "backfire" || g == "blowoff" || g == "starter" || g == "stall"; };
			{
				std::lock_guard<std::mutex> lock(shotMtx);
				shotWork.swap(shotQueue);
			}
			for (const ShotReq& r : shotWork)
				if (auto* v = findShots(E, F, r.group)) startShot(*v, r.gain * (isEngineGroup(r.group) ? cur.engineVolume : fx));
			shotWork.clear();

			const std::vector<Clip>* bfShots = findShots(E, F, "backfire");
			if (int n = backfirePending.exchange(0)) { backfireLeft += n; }
			if (shiftPending.exchange(0)) {
				if (auto* v = findShots(E, F, "shift")) startShot(*v, 0.8f * fx);
				else clunkEnv = 1.0f;
			}
			if (blowOffPending.exchange(0)) {
				if (auto* v = findShots(E, F, "blowoff")) startShot(*v, 0.7f * cur.engineVolume);
				else blowEnv = 1.0f;
			}
			float imp = impactPending.exchange(0.0f);
			if (imp > 0.0f) {
				const char* first = imp > 0.45f ? "impact_heavy" : "impact_light";
				const char* second = imp > 0.45f ? "impact_light" : "impact_heavy";
				const std::vector<Clip>* v = findShots(E, F, first);
				if (!v) v = findShots(E, F, second);
				if (v) startShot(*v, (0.35f + 0.65f * imp) * fx);
				else impactEnv = std::max(impactEnv, imp);
			}

			const float dt = 1.0f / kRate;
			float firingHz = cur.rpm / 60.0f * (cur.cylinders * 0.5f);
			float rpmNorm = std::clamp((cur.rpm - cur.idleRpm) / std::max(1.0f, cur.redline - cur.idleRpm), 0.0f, 1.0f);
			float load = cur.throttle;
			float engineGain = cur.engineOn ? (0.32f + 0.38f * load + 0.15f * rpmNorm) * cur.engineVolume : 0.0f;
			float filterCut = 350.0f + 2600.0f * (0.25f + 0.75f * load) * (0.4f + rpmNorm);
			float speed = cur.speed;
			float speedNorm = std::clamp(speed / 50.0f, 0.0f, 1.0f);

			// Recorded engine: on-load and off-load sets, crossfaded by throttle.
			// With only one set, off-throttle is the same loops, quieter and duller.
			const std::vector<Clip>* onSet = nullptr; const std::vector<Clip>* offSet = nullptr;
			if (E) {
				if (!E->on.empty()) { onSet = &E->on; if (!E->off.empty()) offSet = &E->off; }
				else if (!E->off.empty()) onSet = &E->off;
			}
			Pair pOn = onSet ? pickPair(*onSet, cur.rpm) : Pair{};
			Pair pOff = offSet ? pickPair(*offSet, cur.rpm) : Pair{};
			float wOn = std::sqrt(std::clamp(load, 0.0f, 1.0f)), wOff = std::sqrt(std::clamp(1.0f - load, 0.0f, 1.0f));
			engineFade += ((cur.engineOn ? 1.0f : 0.0f) - engineFade) * 0.25f;
			const float offA = 1.0f - std::exp(-kTau * 900.0f / kRate);

			// Recorded loops and their levels for this buffer.
			const Clip* loops[LCount];
			for (int k = 0; k < LCount; ++k) {
				loops[k] = nullptr;
				for (const Bank* b : { E, F }) {
					if (!b || loops[k]) continue;
					auto it = b->loops.find(kLoopNames[k]);
					if (it != b->loops.end()) loops[k] = &it->second;
				}
			}
			float loopGain[LCount] = {
				cur.gravel * (0.15f + speedNorm) * 0.9f * fx,
				cur.tarmac * std::pow(speedNorm, 0.7f) * 0.6f * fx,
				cur.tarmacSkid * 0.8f * fx,
				cur.snow * (0.15f + speedNorm) * 0.9f * fx,
				cur.grass * (0.15f + speedNorm) * 0.8f * fx,
				speedNorm * speedNorm * 0.7f * fx,
				cur.rain * 0.5f * fx,
				cur.boost * cur.boost * 0.35f * cur.engineVolume * engineFade,
			};
			float loopPitch[LCount] = {
				0.8f + 0.45f * speedNorm, 0.8f + 0.5f * speedNorm, 0.95f + 0.1f * cur.tarmacSkid,
				0.85f + 0.3f * speedNorm, 0.85f + 0.3f * speedNorm, 0.7f + 0.6f * speedNorm, 1.0f,
				0.7f + 0.6f * cur.boost,
			};

			for (unsigned i = 0; i < frames; ++i) {
				float sample = 0.0f;
				if (onSet) {
					float eOn = playPair(*onSet, pOn, posOn);
					float e;
					if (offSet) e = eOn * wOn + playPair(*offSet, pOff, posOff) * wOff;
					else { offLp += (eOn - offLp) * offA; e = eOn * wOn + offLp * wOff * 0.6f; }
					sample += e * engineFade * cur.engineVolume * 0.9f;
				}
				else {
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
					sample += engine * engineGain;
				}

				// Turbo whistle.
				if (loops[LTurbo]) sample += tap(*loops[LTurbo], loopPos[LTurbo], loopPitch[LTurbo]) * loopGain[LTurbo];
				else if (cur.engineOn) {
					turboPhase += (1800.0f + 3200.0f * cur.boost) * dt;
					if (turboPhase >= 1.0f) turboPhase -= 1.0f;
					sample += std::sin(kTau * turboPhase) * 0.025f * cur.boost * cur.boost * engineGain;
				}

				// --- Backfire pops (anti-lag/overrun crackle) ---
				if (backfireLeft > 0 && backfireEnv < 0.05f) {
					backfireGap -= dt;
					if (backfireGap <= 0.0f) {
						--backfireLeft;
						backfireGap = 0.04f + 0.08f * (whiteNoise() * 0.5f + 0.5f);
						if (bfShots) startShot(*bfShots, 0.8f * cur.engineVolume);
						else backfireEnv = 1.0f;
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

				// --- Surfaces (recorded loops where available) ---
				for (int k = LGravel; k <= LRain; ++k)
					if (loops[k] && loopGain[k] > 0.0005f) sample += tap(*loops[k], loopPos[k], loopPitch[k]) * loopGain[k];
				if (!loops[LGravel] && cur.gravel > 0.01f) {
					// Sparse stone clicks over a rumble bed.
					if (whiteNoise() > 1.0f - 0.004f * cur.gravel * (0.3f + speedNorm * 2.0f)) gravelClickEnv = 0.7f + 0.3f * whiteNoise();
					gravelFilter.run(whiteNoise(), 1600.0f, 0.6f);
					sample += (gravelFilter.bp * 0.35f + whiteNoise() * gravelClickEnv * 0.5f) * cur.gravel * (0.2f + speedNorm) * fx;
					gravelClickEnv *= 0.97f;
				}
				if (!loops[LSkid] && cur.tarmacSkid > 0.01f) {
					skidFilter.run(whiteNoise(), 950.0f + 150.0f * std::sin(kTau * 3.0f * i * dt), 0.08f);
					sample += skidFilter.bp * 0.22f * cur.tarmacSkid * fx;
				}
				float softSurf = (loops[LSnow] ? 0.0f : cur.snow) + (loops[LGrass] ? 0.0f : cur.grass * 0.6f);
				if (softSurf > 0.01f) {
					snowFilter.run(whiteNoise(), 500.0f, 0.5f);
					sample += snowFilter.lp * softSurf * 0.5f * (0.2f + speedNorm) * fx;
				}

				// --- Wind & rain ---
				if (!loops[LWind]) {
					windFilter.run(whiteNoise(), 300.0f + speed * 15.0f, 0.6f);
					sample += windFilter.lp * speedNorm * speedNorm * 0.35f * fx;
				}
				if (!loops[LRain] && cur.rain > 0.01f) {
					rainFilter.run(whiteNoise(), 5000.0f, 0.9f);
					sample += rainFilter.hp * cur.rain * 0.12f * fx;
				}

				// --- Impacts ---
				if (impactEnv > 0.001f) {
					impactFilter.run(whiteNoise(), 300.0f + 1500.0f * impactEnv, 0.4f);
					sample += impactFilter.lp * impactEnv * 2.2f * fx;
					impactEnv *= 0.99965f;
				}

				// --- Recorded one-shots ---
				for (Play& p : plays) {
					if (!p.c) continue;
					size_t n = p.c->s.size();
					size_t k = (size_t)p.pos;
					if (k + 1 >= n) { p.c = nullptr; continue; }
					float f = (float)(p.pos - (double)k);
					sample += (p.c->s[k] + (p.c->s[k + 1] - p.c->s[k]) * f) * p.gain;
					p.pos += p.pitch;
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
			if (capturing.load()) {
				std::lock_guard<std::mutex> lock(captureMtx);
				for (unsigned i = 0; i < frames; ++i) captureBuf.push_back(out[i * 2]);
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

	void loadEnginePack(const std::string& dir) {
		static std::string current = "\x01";
		if (dir == current) return;
		current = dir;
		std::shared_ptr<const Bank> b = loadBank(dir, true);
		logBank("engine pack", dir, *b);
		{
			std::lock_guard<std::mutex> lock(bankMtx);
			engineBank = b;
		}
		++bankGen;
	}

	void loadEffects(const std::string& dir) {
		std::shared_ptr<const Bank> b = loadBank(dir, false);
		logBank("effects", dir, *b);
		{
			std::lock_guard<std::mutex> lock(bankMtx);
			fxBank = b;
		}
		++bankGen;
	}

	bool hasEffect(const std::string& group) {
		std::shared_ptr<const Bank> eng, fxb;
		{
			std::lock_guard<std::mutex> lock(bankMtx);
			eng = engineBank; fxb = fxBank;
		}
		return findShots(eng.get(), fxb.get(), group) != nullptr;
	}

	void startCapture() {
		std::lock_guard<std::mutex> lock(captureMtx);
		captureBuf.clear();
		captureBuf.reserve(kRate * 60);
		capturing = true;
	}

	void stopCapture(const std::string& wavPath) {
		capturing = false;
		std::lock_guard<std::mutex> lock(captureMtx);
		if (captureBuf.empty()) return;
		std::vector<short> pcm(captureBuf.size());
		for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (short)std::clamp(captureBuf[i] * 32767.0f, -32768.0f, 32767.0f);
		Wave w = { (unsigned)pcm.size(), (unsigned)kRate, 16, 1, pcm.data() };
		ExportWave(w, wavPath.c_str());
		log::info("audio: captured %.1f s to %s", pcm.size() / (float)kRate, wavPath.c_str());
		captureBuf.clear();
	}

	void playEffect(const std::string& group, float gain) {
		std::lock_guard<std::mutex> lock(shotMtx);
		if (shotQueue.size() < 32) shotQueue.push_back({ group, gain });
	}
}
