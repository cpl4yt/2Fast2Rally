#include "game/game.h"
#include "core/input.h"
#include "core/ui.h"
#include "core/log.h"
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <ctime>
#include <algorithm>

namespace rally {
	namespace {
		constexpr float kStep = 1.0f / 120.0f;

		CarVisual lerpVisual(const CarVisual& a, const CarVisual& b, float t) {
			CarVisual v = b;
			v.position = Vector3Lerp(a.position, b.position, t);
			v.rotation = QuaternionSlerp(a.rotation, b.rotation, t);
			v.velocity = Vector3Lerp(a.velocity, b.velocity, t);
			for (int i = 0; i < 4; ++i) v.wheelOffset[i] = a.wheelOffset[i] + (b.wheelOffset[i] - a.wheelOffset[i]) * t;
			v.steerAngle = a.steerAngle + (b.steerAngle - a.steerAngle) * t;
			v.steerInput = a.steerInput + (b.steerInput - a.steerInput) * t;
			v.rpm = a.rpm + (b.rpm - a.rpm) * t;
			return v;
		}

		std::string todayString() {
			char buf[32];
			std::time_t t = std::time(nullptr);
			std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&t));
			return buf;
		}
	}

	void Game::setMessage(const std::string& m, Color c, float t) {
		run_.message = m;
		run_.messageColor = c;
		run_.messageTimer = t;
	}

	const CarVisual& Game::displayCar() const {
		return run_.role == Role::Driver ? renderVisual_ : remoteVisual_;
	}

	bool Game::startStage(int stageIndex, int carIndex, Weather w, TimeOfDay t, Role role, bool online, const nlohmann::json* resume) {
		endStage();
		if (stageDefs_.empty() || cars_.empty()) return false;
		stageIndex_ = std::clamp(stageIndex, 0, (int)stageDefs_.size() - 1);
		carIndex_ = std::clamp(carIndex, 0, (int)cars_.size() - 1);
		weather_ = w;
		timeOfDay_ = t;

		// Loading frame (building the stage blocks for a moment).
		EndDrawing();
		BeginDrawing();
		ClearBackground(Color{ 16, 18, 22, 255 });
		float s = ui::s();
		ui::text("LOADING", GetScreenWidth() * 0.5f, GetScreenHeight() * 0.42f, 70 * s, ui::kAccent, ui::Center, true);
		ui::text(stageDefs_[stageIndex_].name + "  -  " + cars_[carIndex_].name, GetScreenWidth() * 0.5f, GetScreenHeight() * 0.52f, 34 * s, ui::kText, ui::Center);
		ui::text(role == Role::Driver ? "You are the DRIVER" : "You are the NAVIGATOR", GetScreenWidth() * 0.5f, GetScreenHeight() * 0.58f, 28 * s, ui::kTextDim, ui::Center);
		EndDrawing();
		BeginDrawing();
		ClearBackground(BLACK);

		run_ = RunState{};
		run_.role = role;
		run_.online = online;
		run_.aiCodriver = !online && role == Role::Driver;
		run_.aiDriver = (!online && role == Role::Navigator) || auto_.aiDriver || auto_.selfTest || (auto_.netHost && role == Role::Driver) || (auto_.netJoin && role == Role::Driver);

		if (!stage_.build(stageDefs_[stageIndex_], w, t)) {
			log::error("stage build failed");
			return false;
		}
		world_.setStage(&stage_);
		notes_ = generatePaceNotes(stage_);
		run_.calls.assign(notes_.size(), NoteCallState{});
		log::info("stage: %d pace notes", (int)notes_.size());

		physics_.clear();
		physics_.setGravity(9.81f);
		physics_.setTerrain([this](float x, float z) { return stage_.heightAt(x, z); }, true);
		RoadSample s0 = stage_.sampleAt(stage_.startS - 12.0f);
		float yaw = std::atan2(s0.dir.x, s0.dir.y);
		car_.init(physics_, cars_[carIndex_], { s0.pos.x, s0.pos.y + 0.8f, s0.pos.z }, yaw);
		car_.manualGearbox = settings().manualGearbox;
		car_.absEnabled = settings().abs;
		car_.tcsEnabled = settings().tcs || run_.aiDriver;
		car_.antiFlip = settings().antiFlip;
		car_.engineEnabled = false;
		carRender_.build(cars_[carIndex_]);
		particles_.clear();
		snapshots_.clear();
		accumulator_ = 0;
		seenBackfire_ = seenShift_ = seenImpact_ = 0;

		// Settle the suspension before anyone sees it.
		DriveInput hold; hold.brake = 1; hold.handbrake = true;
		for (int i = 0; i < 90; ++i) {
			car_.update(kStep, hold, stage_);
			stage_.nearbyStatics(car_.position(), 30, statics_);
			physics_.step(kStep, statics_);
			car_.postStep(kStep);
		}
		curVisual_ = prevVisual_ = renderVisual_ = remoteVisual_ = car_.visual();
		run_.progress = stage_.project(car_.position(), run_.hint, &run_.lateral);
		run_.lastOnRoadS = run_.progress;

		if (resume) {
			const auto& r = *resume;
			run_.stageTime = r.value("time", 0.0);
			run_.progress = r.value("progress", 0.0f);
			run_.penalties = r.value("penalties", 0.0);
			run_.resets = r.value("resets", 0);
			run_.splitIndex = r.value("splitIndex", 0);
			run_.splitTimes[0] = r.value("split0", 0.0f);
			run_.splitTimes[1] = r.value("split1", 0.0f);
			if (role == Role::Driver && r.contains("pos")) {
				auto p = r["pos"]; auto q = r["rot"];
				Quaternion rot = { q[0], q[1], q[2], q[3] };
				car_.resetTo({ p[0], p[1].get<float>() + 0.3f, p[2] }, yawOf(rot));
			}
			for (size_t i = 0; i < notes_.size(); ++i)
				if (notes_[i].s < run_.progress + 30) { run_.calls[i].called = true; run_.calls[i].passed = notes_[i].s < run_.progress; }
			run_.resumeFromSnapshot = true;
		}

		run_.phase = online ? RunPhase::WaitingPartner : RunPhase::Countdown;
		run_.countdown = resume ? 3.5f : 4.0f;
		if (online) session_.sendStageEvent({ {"kind", "loaded"} });

		camInit_ = false;
		camMode_ = settings().defaultCamera;
		navView_ = auto_.navView >= 0 ? auto_.navView : 0;
		synth::setPaused(false);
		screen_ = Screen::Stage;
		ui::resetFocus();
		if (!resume) voice_.sayOne("goodluck", true);
		return true;
	}

	void Game::endStage() {
		if (!stage_.loaded()) return;
		car_.destroy();
		physics_.clear();
		world_.setStage(nullptr);
		stage_.unload();
		carRender_.unload();
		particles_.clear();
		voice_.clear();
		synth::setPaused(true);
		notes_.clear();
	}

	// ------------------------------------------------------------------
	// Per-frame
	// ------------------------------------------------------------------
	void Game::stageFrame(float dt) {
		Settings& cfg = settings();
		bool driver = run_.role == Role::Driver;

		// --- UI-level inputs ---
		bool overlay = run_.paused || (run_.phase == RunPhase::Finished && run_.finishedFor > 2.5f);
		if (input::pressed(Action::Pause) && run_.phase != RunPhase::Finished && !run_.partnerLost) {
			run_.paused = !run_.paused;
			if (run_.online) session_.sendPause(run_.paused);
			ui::resetFocus();
		}
		if (!overlay) {
			if (driver && input::pressed(Action::Camera)) camMode_ = (camMode_ + 1) % 4;
			if (!driver && input::pressed(Action::NavView)) navView_ = (navView_ + 1) % 4;
			if (!driver && run_.aiDriver && input::pressed(Action::Camera)) navView_ = (navView_ + 1) % 4;
		}
		if (input::pressed(Action::Debug)) debugOverlay_ = !debugOverlay_;
		if (IsKeyPressed(KEY_F5) && driver) {
			reloadCars();
			if (carIndex_ < (int)cars_.size()) { car_.applySpec(cars_[carIndex_]); setMessage("Car tuning reloaded", ui::kAccent); }
		}

		// --- Phases ---
		if (!run_.paused) {
			switch (run_.phase) {
			case RunPhase::WaitingPartner:
				run_.waitTimer += dt;
				if (driver && (run_.partnerLoaded || run_.waitTimer > 25.0f || !run_.online)) {
					run_.phase = RunPhase::Countdown;
					if (run_.online) session_.sendStageEvent({ {"kind", "countdown"}, {"seconds", run_.countdown} });
				}
				break;
			case RunPhase::Countdown: {
				run_.countdown -= dt;
				int sec = (int)std::ceil(run_.countdown);
				if (sec != run_.lastBeep && sec <= 3 && sec >= 1) { voice_.beep(false); run_.lastBeep = sec; }
				if (run_.countdown <= 0.0f) {
					run_.phase = RunPhase::Running;
					voice_.beep(true);
					car_.engineEnabled = true;
					setMessage("GO!", ui::kGood, 1.2f);
				}
				break;
			}
			case RunPhase::Finished:
				run_.finishedFor += dt;
				break;
			default: break;
			}
		}
		if (run_.messageTimer > 0) run_.messageTimer -= dt;

		// --- Simulation (driver owns the car) ---
		if (driver) {
			if (!run_.paused) {
				accumulator_ += dt;
				int guard = 0;
				while (accumulator_ >= kStep && guard++ < 12) {
					prevVisual_ = curVisual_;
					simStep(kStep);
					curVisual_ = car_.visual();
					accumulator_ -= kStep;
				}
				if (guard >= 12) accumulator_ = 0;
				renderVisual_ = lerpVisual(prevVisual_, curVisual_, (float)(accumulator_ / kStep));
			}
			if (run_.online) {
				sendTimer_ += dt;
				if (sendTimer_ >= 1.0f / 30.0f) { sendTimer_ = 0; sendCarState(); }
			}
			if (run_.aiCodriver && !run_.paused && (run_.phase == RunPhase::Running || run_.phase == RunPhase::Countdown)) aiCodriver();
		}
		else {
			// Navigator: interpolate the driver's snapshots ~100 ms in the past.
			double now = GetTime();
			double renderTime = now - 0.1;
			if (snapshots_.size() >= 2) {
				size_t i = 0;
				while (i + 2 < snapshots_.size() && snapshots_[i + 1].recvTime < renderTime) ++i;
				const Snapshot& a = snapshots_[i];
				const Snapshot& b = snapshots_[i + 1];
				float t = (float)std::clamp((renderTime - a.recvTime) / std::max(1e-3, b.recvTime - a.recvTime), 0.0, 1.0);
				CarVisual va, vb;
				unpackCar(a.pkt, va);
				unpackCar(b.pkt, vb);
				remoteVisual_ = lerpVisual(va, vb, t);
			}
			else if (!snapshots_.empty()) unpackCar(snapshots_.back().pkt, remoteVisual_);

			if (!run_.paused && run_.phase == RunPhase::Running) {
				// Human navigator calls.
				if (!run_.aiDriver || true) {
					if (input::pressed(Action::CallNote)) {
						for (int i = 0; i < (int)notes_.size(); ++i) {
							if (run_.calls[i].called || notes_[i].sEnd < run_.progress - 5) continue;
							callNote(i, false);
							break;
						}
					}
				}
				if (run_.aiDriver) {
					// Navigator practice: AI drives locally (solo), human calls.
					accumulator_ += dt;
					int guard = 0;
					while (accumulator_ >= kStep && guard++ < 12) {
						prevVisual_ = curVisual_;
						simStep(kStep);
						curVisual_ = car_.visual();
						accumulator_ -= kStep;
					}
					if (guard >= 12) accumulator_ = 0;
					remoteVisual_ = lerpVisual(prevVisual_, curVisual_, (float)(accumulator_ / kStep));
				}
			}
			// Navigator auto-test: call notes like the AI would.
			if ((auto_.netJoin || auto_.selfTest) && run_.phase == RunPhase::Running && !run_.paused) aiCodriver();
			if (!run_.online && run_.aiDriver && (run_.phase == RunPhase::Countdown) && !run_.paused) {
				// keep the practice car still during the countdown
				accumulator_ = 0;
			}
		}
		if (run_.partnerLost && run_.online) run_.paused = true;

		updateAudio(dt);
		const CarVisual& dv = displayCar();
		if (!run_.paused) {
			bool backfire = false;
			if (driver) { if (run_.backfireSeq != seenBackfire_) { backfire = true; } }
			particles_.emitForCar(dv, cars_[carIndex_], dt, cfg.particleQuality, backfire);
			particles_.update(dt);
		}
		updateCamera(dt);
		drawStage3D(run_.paused ? 0.0f : dt);
		drawHud();
	}

	// ------------------------------------------------------------------
	// Fixed step
	// ------------------------------------------------------------------
	void Game::driverInput(DriveInput& in, float dt) {
		(void)dt;
		if (run_.aiDriver) aiDrive(in);
		else in = input::drive();
	}

	void Game::simStep(float h) {
		DriveInput in;
		driverInput(in, h);
		if (run_.phase != RunPhase::Running && run_.phase != RunPhase::Finished) {
			in.brake = 1.0f;
			in.handbrake = true;
		}
		if (run_.phase == RunPhase::Finished) {
			// Roll to a stop after the finish line.
			DriveInput ai; aiDrive(ai);
			in.steer = ai.steer;
			in.throttle = 0;
			in.brake = 1.0f;
			in.handbrake = car_.speed() < 1.0f;
		}

		stage_.nearbyStatics(car_.position(), 30.0f, statics_);
		car_.update(h, in, stage_);
		physics_.step(h, statics_);
		car_.postStep(h);

		if (car_.pendingShift) { car_.pendingShift = false; ++run_.shiftSeq; synth::triggerShift(); }
		if (car_.pendingBackfires > 0) { synth::triggerBackfire(car_.pendingBackfires); car_.pendingBackfires = 0; ++run_.backfireSeq; }
		if (car_.pendingBlowOff) { car_.pendingBlowOff = false; synth::triggerBlowOff(); }
		if (car_.lastImpact > 0.0f) {
			float strength = std::clamp((car_.lastImpact - 2.5f) / 10.0f, 0.15f, 1.0f);
			synth::triggerImpact(strength);
			run_.impactStrength = strength;
			++run_.impactSeq;
			particles_.emitImpact(car_.position(), Color{ 150, 140, 120, 180 }, 10);
			car_.lastImpact = 0.0f;
		}

		if (run_.phase == RunPhase::Running) {
			run_.stageTime += h;
		}
		run_.simClock += h;
		updateProgress();

		if (run_.phase == RunPhase::Running && !run_.aiDriver && input::pressed(Action::Reset)) resetToRoad(true, "Reset");
		// The AI can't reverse out of trouble: if it's been stuck, put it back.
		if (run_.phase == RunPhase::Running && run_.aiDriver) {
			aiStuckTimer_ = std::fabs(car_.speed()) < 1.0f ? aiStuckTimer_ + h : 0.0f;
			if (aiStuckTimer_ > 4.0f) {
				aiStuckTimer_ = 0;
				// Stuck at the same place again: move on a little further.
				bool repeat = std::fabs(run_.progress - aiLastStuckS_) < 25.0f;
				aiLastStuckS_ = run_.progress;
				if (repeat) run_.progress += 20.0f;
				resetToRoad(true, "Stuck");
			}
		}
	}

	void Game::updateProgress() {
		float prev = run_.progress;
		run_.progress = stage_.project(car_.position(), run_.hint, &run_.lateral);
		const float hw = stage_.halfWidth();
		RoadSample rs = stage_.sampleAt(run_.progress);
		Vector3 p = car_.position();

		if (run_.phase == RunPhase::Running) {
			// Splits.
			if (run_.splitIndex < 2 && run_.progress >= stage_.splitS[run_.splitIndex] && prev < stage_.splitS[run_.splitIndex] + 50) {
				run_.splitTimes[run_.splitIndex] = (float)(run_.stageTime + run_.penalties);
				auto best = leaderboard_.bestSplits(stage_.def.id);
				std::string msg = "SPLIT " + std::to_string(run_.splitIndex + 1) + "  " + ui::formatTime(run_.splitTimes[run_.splitIndex]);
				Color c = ui::kAccent;
				if ((int)best.size() > run_.splitIndex) {
					double d = run_.splitTimes[run_.splitIndex] - best[run_.splitIndex];
					msg += "   " + ui::formatTime(d, true);
					c = d <= 0 ? ui::kGood : ui::kBad;
				}
				// Split deltas go to the navigator in co-op (they relay them).
				if (!run_.online || run_.role == Role::Navigator) setMessage(msg, c, 3.0f);
				else setMessage("SPLIT " + std::to_string(run_.splitIndex + 1), ui::kAccent, 2.0f);
				run_.splitIndex++;
			}
			// Finish.
			if (run_.progress >= stage_.finishS && std::fabs(run_.lateral) < hw + 15.0f) finishRun();

			// Off-road / cut tracking.
			bool onRoad = std::fabs(run_.lateral) < hw + 1.2f;
			if (onRoad) {
				// Rejoined much further along the road than the distance we
				// actually drove off it: that's a cut corner.
				float gain = run_.progress - run_.lastOnRoadS;
				if (run_.offRoadTimer > 0.0f && gain > 25.0f && gain > run_.offRoadDist * 1.3f) {
					run_.cuts++;
					run_.penalties += 2.0;
					setMessage("Corner cut  +2s", ui::kBad, 2.0f);
				}
				run_.lastOnRoadS = run_.progress;
				run_.offRoadTimer = 0.0f;
				run_.offRoadDist = 0.0f;
			}
			else {
				run_.offRoadTimer += kStep;
				run_.offRoadDist += Vector3Length(car_.velocity()) * kStep;
			}
			bool lost = std::fabs(run_.lateral) > hw + 28.0f || p.y < rs.pos.y - 7.0f;
			run_.lostTimer = lost ? run_.lostTimer + kStep : 0.0f;
			if (run_.lostTimer > 2.5f) { run_.lostTimer = 0; resetToRoad(true, "Off course"); }
			if (car_.timeUpsideDown > 3.0f) resetToRoad(true, "Rolled");
		}

		// Notes: passed / missed, HUD queue cleanup.
		for (size_t i = 0; i < notes_.size(); ++i) {
			NoteCallState& c = run_.calls[i];
			if (!c.passed && notes_[i].s < run_.progress - 3.0f) {
				c.passed = true;
				if (!c.called && run_.phase == RunPhase::Running && (run_.role == Role::Navigator || run_.online)) ++run_.missedCalls;
				if (!c.called) c.called = true; // too late now
			}
		}
		while (!run_.hudNotes.empty() && notes_[run_.hudNotes.front()].sEnd < run_.progress - 8.0f) run_.hudNotes.pop_front();
	}

	void Game::resetToRoad(bool penalty, const char* why) {
		Vector3 cp = car_.position();
		log::info("reset: %s at s=%.0f lat=%.1f speed=%.1f pos=(%.0f,%.1f,%.0f) up=%.2f", why, run_.progress, run_.lateral, car_.speed() * 3.6f, cp.x, cp.y, cp.z, car_.up().y);
		float s = std::max(stage_.startS - 10.0f, run_.progress - 6.0f);
		RoadSample rs = stage_.sampleAt(s);
		float yaw = std::atan2(rs.dir.x, rs.dir.y);
		car_.resetTo({ rs.pos.x, rs.pos.y + 0.9f, rs.pos.z }, yaw);
		run_.hint = -1;
		run_.offRoadTimer = 0;
		run_.offRoadDist = 0;
		run_.lostTimer = 0;
		run_.lastOnRoadS = s;
		if (penalty && run_.phase == RunPhase::Running) {
			run_.penalties += 5.0;
			run_.resets++;
			setMessage(std::string(why) + "  +5s", ui::kBad, 2.5f);
			if (run_.online) session_.sendStageEvent({ {"kind", "message"}, {"text", std::string(why) + "  +5s"} });
		}
		prevVisual_ = curVisual_ = car_.visual();
	}

	void Game::finishRun() {
		if (run_.phase == RunPhase::Finished) return;
		run_.phase = RunPhase::Finished;
		run_.finishedFor = 0;
		run_.finishTime = run_.stageTime + run_.penalties;
		setMessage("FINISH  " + ui::formatTime(run_.finishTime), ui::kAccent, 4.0f);
		voice_.sayOne("finished");
		if (run_.online && run_.role == Role::Driver) {
			session_.sendStageEvent({ {"kind", "finish"}, {"stageTime", run_.stageTime}, {"penalties", run_.penalties},
				{"resets", run_.resets}, {"cuts", run_.cuts}, {"split0", run_.splitTimes[0]}, {"split1", run_.splitTimes[1]} });
		}
		if (!run_.recorded && !auto_.selfTest) {
			RunRecord r;
			r.time = run_.finishTime;
			r.penalties = run_.penalties;
			r.resets = run_.resets;
			std::string me = settings().playerName;
			std::string partner = run_.online ? session_.partnerName() : (run_.aiDriver ? "AI" : "AI co-driver");
			r.driver = run_.role == Role::Driver ? me : partner;
			r.navigator = run_.role == Role::Driver ? partner : me;
			r.car = cars_[carIndex_].name;
			r.date = todayString();
			r.splits = { run_.splitTimes[0], run_.splitTimes[1] };
			run_.rank = leaderboard_.add(stage_.def.id, r);
			run_.recorded = true;
		}
	}

	// ------------------------------------------------------------------
	// AI
	// ------------------------------------------------------------------
	void Game::aiDrive(DriveInput& in) {
		const CarSpec& spec = cars_[carIndex_];
		float speed = car_.speed();
		Vector3 pos = car_.position();
		// Shorter look-ahead in tight corners so the AI doesn't cut the inside,
		// and aim a little wide of the centre line.
		float kHere = std::fabs(stage_.sampleAt(run_.progress + 6.0f).curvature);
		float look = std::clamp(3.5f + speed * 0.45f, 4.5f, 26.0f) / (1.0f + kHere * 25.0f);
		look = std::max(look, 3.0f);
		RoadSample t = stage_.sampleAt(run_.progress + look);
		float wide = std::clamp(kHere * 40.0f, 0.0f, 1.2f) * (t.curvature > 0 ? 1.0f : -1.0f); // + = toward the right
		Vector3 aim = { t.pos.x + t.right.x * wide, t.pos.y, t.pos.z + t.right.y * wide };
		Vector3 local = Vector3RotateByQuaternion(Vector3Subtract(aim, pos), QuaternionInvert(car_.rotation()));
		float angle = std::atan2(-local.x, std::max(0.5f, local.z));
		float maxSteer = spec.maxSteerDeg * DEG2RAD;
		in.steer = std::clamp(angle * 1.6f / maxSteer, -1.0f, 1.0f);

		// Target speed from upcoming curvature and grip.
		float vmax = 55.0f;
		float horizon = std::max(50.0f, speed * 3.5f);
		for (float d = 0; d < horizon; d += 4.0f) {
			RoadSample q = stage_.sampleAt(run_.progress + d);
			int idx = std::clamp((int)(run_.progress + d), 0, (int)stage_.samples.size() - 1);
			float k = 0;
			for (int j = std::max(0, idx - 6); j <= std::min((int)stage_.samples.size() - 1, idx + 6); ++j) k = std::max(k, std::fabs(stage_.samples[j].curvature));
			float mu = surfaceProps(q.surface).friction * spec.grip * stage_.gripMultiplier(q.surface);
			float vc = std::sqrt(mu * 9.81f * 0.6f / std::max(k, 1e-4f));
			float decel = mu * 9.81f * 0.42f;
			float allowed = std::sqrt(vc * vc + 2.0f * decel * d);
			vmax = std::min(vmax, allowed);
		}
		for (auto& j : stage_.def.jumps) {
			float d = j.at - run_.progress;
			if (d > 0 && d < 80) vmax = std::min(vmax, 24.0f);
		}
		in.throttle = std::clamp((vmax - speed) * 0.35f, 0.0f, 1.0f);
		in.brake = std::clamp((speed - vmax) * 0.25f, 0.0f, 1.0f);
		if (speed < 3.0f && run_.phase == RunPhase::Running) { in.throttle = std::max(in.throttle, 0.6f); in.brake = 0; }
		in.handbrake = false;
	}

	void Game::aiCodriver() {
		float speed = std::max(6.0f, std::fabs(displayCar().speed));
		float lead = std::clamp(speed * 4.0f, 70.0f, 250.0f);
		if (voice_.busy()) return;
		for (int i = 0; i < (int)notes_.size(); ++i) {
			if (run_.calls[i].called) continue;
			if (notes_[i].sEnd < run_.progress - 3) continue;
			if (notes_[i].s - run_.progress <= lead) {
				callNote(i, false);
				// Chain "into"/"and" calls like a real co-driver.
				if ((notes_[i].link == "into" || notes_[i].link == "and") && i + 1 < (int)notes_.size()) callNote(i + 1, false);
			}
			break;
		}
	}

	void Game::callNote(int idx, bool fromNetwork) {
		if (idx < 0 || idx >= (int)notes_.size()) return;
		NoteCallState& c = run_.calls[idx];
		if (c.called && !(fromNetwork && !c.passed)) return;
		bool alreadyCalled = c.called;
		c.called = true;
		const PaceNote& n = notes_[idx];
		float dist = n.s - run_.progress;
		float speed = std::max(4.0f, std::fabs(displayCar().speed));
		c.callDistance = dist;
		c.callTime = dist / speed;
		bool human = (run_.role == Role::Navigator && !run_.aiCodriver && !auto_.netJoin) || (run_.role == Role::Driver && fromNetwork);
		if (human || run_.role == Role::Navigator || fromNetwork) {
			if (dist < 0 || c.callTime < 1.5f) ++run_.lateCalls;
			else if (c.callTime > 7.0f && dist > 120.0f) ++run_.earlyCalls;
			else ++run_.goodCalls;
		}
		else ++run_.goodCalls;
		if (settings().aiCodriverVoice && !alreadyCalled) voice_.say(noteWords(n));
		else if (settings().aiCodriverVoice && fromNetwork && run_.role == Role::Driver) voice_.say(noteWords(n));
		if (std::find(run_.hudNotes.begin(), run_.hudNotes.end(), idx) == run_.hudNotes.end()) {
			run_.hudNotes.push_back(idx);
			std::sort(run_.hudNotes.begin(), run_.hudNotes.end());
		}
		if (run_.role == Role::Navigator && run_.online && !fromNetwork) session_.sendNoteCall(idx);
		run_.navCursor = std::max(run_.navCursor, idx + 1);
	}

	// ------------------------------------------------------------------
	// Network sync
	// ------------------------------------------------------------------
	void Game::sendCarState() {
		CarStatePacket p{};
		p.seq = ++sendSeq_;
		p.simTime = run_.simClock;
		p.stageTime = (float)run_.stageTime;
		p.progress = run_.progress;
		p.phase = (uint8_t)run_.phase;
		p.paused = run_.paused ? 1 : 0;
		packCar(curVisual_, p);
		p.resets = (uint8_t)std::min(255, run_.resets);
		p.splitIndex = (uint8_t)run_.splitIndex;
		p.penalties = (float)run_.penalties;
		p.splitTimes[0] = run_.splitTimes[0];
		p.splitTimes[1] = run_.splitTimes[1];
		p.backfireSeq = run_.backfireSeq;
		p.shiftSeq = run_.shiftSeq;
		p.impactSeq = run_.impactSeq;
		p.impactStrength = run_.impactStrength;
		p.cuts = (uint16_t)run_.cuts;
		session_.sendCarState(p);
	}

	void Game::receiveCarState(const CarStatePacket& p) {
		if (!snapshots_.empty() && p.seq <= snapshots_.back().pkt.seq) return;
		snapshots_.push_back({ p, GetTime() });
		while (snapshots_.size() > 12) snapshots_.pop_front();

		if (run_.phase != RunPhase::Finished) {
			run_.stageTime = p.stageTime;
			RunPhase dp = (RunPhase)p.phase;
			if (dp == RunPhase::Running && (run_.phase == RunPhase::Countdown || run_.phase == RunPhase::WaitingPartner)) run_.phase = RunPhase::Running;
		}
		float prevProgress = run_.progress;
		run_.progress = p.progress;
		if (p.splitIndex > run_.splitIndex) {
			int idx = p.splitIndex - 1;
			run_.splitIndex = p.splitIndex;
			run_.splitTimes[0] = p.splitTimes[0];
			run_.splitTimes[1] = p.splitTimes[1];
			auto best = leaderboard_.bestSplits(stage_.def.id);
			std::string msg = "SPLIT " + std::to_string(idx + 1) + "  " + ui::formatTime(run_.splitTimes[idx]);
			Color c = ui::kAccent;
			if ((int)best.size() > idx) {
				double d = run_.splitTimes[idx] - best[idx];
				msg += "   " + ui::formatTime(d, true);
				c = d <= 0 ? ui::kGood : ui::kBad;
			}
			setMessage(msg, c, 3.5f);
		}
		run_.penalties = p.penalties;
		run_.resets = p.resets;
		run_.cuts = p.cuts;
		(void)prevProgress;
		// Mark passed/missed notes on the navigator side too.
		for (size_t i = 0; i < notes_.size(); ++i) {
			NoteCallState& c = run_.calls[i];
			if (!c.passed && notes_[i].s < run_.progress - 3.0f) {
				c.passed = true;
				if (!c.called && run_.phase == RunPhase::Running) ++run_.missedCalls;
				c.called = true;
			}
		}
		while (!run_.hudNotes.empty() && notes_[run_.hudNotes.front()].sEnd < run_.progress - 8.0f) run_.hudNotes.pop_front();
	}

	// ------------------------------------------------------------------
	// Camera & audio
	// ------------------------------------------------------------------
	void Game::updateCamera(float dt) {
		const CarVisual& v = displayCar();
		Matrix rot = QuaternionToMatrix(v.rotation);
		Vector3 fwd = Vector3Transform({ 0, 0, 1 }, rot);
		Vector3 up = Vector3Transform({ 0, 1, 0 }, rot);
		float carYaw = std::atan2(fwd.x, fwd.z);
		if (!camInit_) { camYaw_ = carYaw; camInit_ = true; camPos_ = v.position; }
		float dy = carYaw - camYaw_;
		while (dy > PI) dy -= 2 * PI;
		while (dy < -PI) dy += 2 * PI;
		camYaw_ += dy * std::min(1.0f, dt * 5.0f);
		Vector3 flat = { std::sin(camYaw_), 0, std::cos(camYaw_) };
		bool lookBack = input::down(Action::LookBack) && run_.role == Role::Driver;
		if (lookBack) flat = Vector3Negate(flat);

		int mode;
		if (run_.role == Role::Driver) mode = camMode_;
		else mode = navView_ == 1 ? 0 : 4; // navigator: chase or co-driver seat
		float speed = Vector3Length(v.velocity);
		float fov = settings().fov + std::clamp(speed - 15.0f, 0.0f, 30.0f) * 0.2f;

		auto attach = [&](Vector3 local, float pitchDown) {
			Vector3 eye = Vector3Add(v.position, Vector3Transform(local, rot));
			Vector3 dir = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(up, pitchDown)));
			if (lookBack) dir = Vector3Negate(dir);
			cam_.position = eye;
			cam_.target = Vector3Add(eye, dir);
			cam_.up = Vector3Normalize(Vector3Lerp({ 0, 1, 0 }, up, 0.6f));
		};

		switch (mode) {
		case 0:
		case 1: {
			float back = mode == 0 ? 6.4f : 4.4f, height = mode == 0 ? 2.3f : 1.65f;
			Vector3 desired = Vector3Add(Vector3Subtract(v.position, Vector3Scale(flat, back)), { 0, height, 0 });
			float k = std::min(1.0f, dt * 9.0f);
			camPos_ = Vector3Lerp(camPos_, desired, k);
			// Snap if far away (resets / teleports).
			if (Vector3Distance(camPos_, desired) > 25.0f) camPos_ = desired;
			float ground = stage_.heightAt(camPos_.x, camPos_.z) + 0.5f;
			if (camPos_.y < ground) camPos_.y = ground;
			cam_.position = camPos_;
			cam_.target = Vector3Add(Vector3Add(v.position, { 0, 0.9f, 0 }), Vector3Scale(flat, 2.5f));
			cam_.up = { 0, 1, 0 };
			break;
		}
		case 2: attach(carRender_.bonnetEye(), 0.04f); fov -= 4; break;
		case 3: attach(carRender_.driverEye(), 0.06f); fov -= 8; break;
		case 4: attach(carRender_.codriverEye(), 0.1f); fov -= 6; break;
		}
		cam_.fovy = fov;
		cam_.projection = CAMERA_PERSPECTIVE;
	}

	void Game::updateAudio(float dt) {
		(void)dt;
		const CarVisual& v = displayCar();
		const CarSpec& spec = cars_[carIndex_];
		Settings& cfg = settings();
		synth::CarAudioParams p;
		p.rpm = v.rpm;
		p.idleRpm = spec.idleRpm;
		p.redline = spec.redline;
		p.cylinders = spec.cylinders;
		p.throttle = v.throttle;
		p.boost = spec.turbo ? v.boost : 0.0f;
		p.speed = Vector3Length(v.velocity);
		float gravel = 0, skid = 0, snow = 0, grass = 0;
		int contact = 0;
		for (int i = 0; i < 4; ++i) {
			if (!(v.wheelContact & (1 << i))) continue;
			++contact;
			Surface s = (Surface)v.wheelSurface[i];
			float slip = v.wheelSlip[i];
			if (s == Surface::Gravel || s == Surface::Dirt || s == Surface::Mud) gravel += 0.5f + slip;
			else if (s == Surface::Tarmac) skid = std::max(skid, std::max(0.0f, slip - 0.2f) * 1.3f);
			else if (s == Surface::Snow) snow += 0.4f + slip;
			else if (s == Surface::Grass) grass += 0.5f + slip;
		}
		if (contact) { gravel /= contact; snow /= contact; grass /= contact; }
		p.gravel = std::clamp(gravel, 0.0f, 1.0f);
		p.tarmacSkid = std::clamp(skid, 0.0f, 1.0f);
		p.snow = std::clamp(snow, 0.0f, 1.0f);
		p.grass = std::clamp(grass, 0.0f, 1.0f);
		p.rain = (world_.env().precipitation && !world_.env().snowfall) ? 1.0f : 0.0f;
		p.engineVolume = cfg.engineVolume;
		p.effectsVolume = cfg.effectsVolume;
		p.engineOn = true;
		p.interior = (run_.role == Role::Driver && camMode_ >= 2) || (run_.role == Role::Navigator && navView_ != 1);
		synth::setParams(p);
		bool overlayPause = run_.paused;
		synth::setPaused(overlayPause);

		if (run_.role == Role::Navigator && !snapshots_.empty() && !run_.aiDriver) {
			const CarStatePacket& last = snapshots_.back().pkt;
			if (last.backfireSeq != seenBackfire_) { seenBackfire_ = last.backfireSeq; synth::triggerBackfire(3); }
			if (last.shiftSeq != seenShift_) { seenShift_ = last.shiftSeq; synth::triggerShift(); }
			if (last.impactSeq != seenImpact_) { seenImpact_ = last.impactSeq; synth::triggerImpact(last.impactStrength); }
		}
		else {
			seenBackfire_ = run_.backfireSeq;
		}
	}

	void Game::drawStage3D(float dt) {
		const CarVisual& v = displayCar();
		Matrix rot = QuaternionToMatrix(v.rotation);
		Vector3 fwd = Vector3Transform({ 0, 0, 1 }, rot);
		Vector3 lampPos = Vector3Add(v.position, Vector3Transform(carRender_.headlightLocal(), rot));
		Vector3 lampDir = Vector3Normalize(Vector3Add(fwd, { 0, -0.07f, 0 }));
		float lampScale = 1.0f;
		if (v.lightsBroken & 1) lampScale -= 0.3f;
		if (v.lightsBroken & 2) lampScale -= 0.3f;
		if (v.lightsBroken & 4) lampScale -= 0.25f;
		world_.beginFrame(cam_, lampPos, lampDir, true, lampScale);
		world_.drawSky(cam_);
		BeginMode3D(cam_);
		world_.drawStage(cam_, settings().drawDistance);
		bool interior = (run_.role == Role::Driver && camMode_ >= 2) || (run_.role == Role::Navigator && navView_ != 1);
		carRender_.draw(world_, v, dt, world_.env().lampIntensity > 0.0f, interior ? CarView::Interior : CarView::Exterior);
		world_.drawWater();
		particles_.draw(cam_, world_.particleTexture());
		particles_.drawWeather(cam_, dt, world_.env().precipitation && !world_.env().snowfall, world_.env().snowfall, v.velocity);
		EndMode3D();
	}
}
