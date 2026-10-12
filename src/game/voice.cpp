#include "game/voice.h"
#include "game/paceNotes.h"
#include "core/log.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace rally {
	namespace {
		// Trims near-silent frames from both ends of a 16-bit wave.
		void trimSilence(Wave& w) {
			if (w.sampleSize != 16 || w.frameCount == 0) return;
			const short* d = static_cast<const short*>(w.data);
			int ch = (int)w.channels;
			auto loud = [&](unsigned f) {
				for (int c = 0; c < ch; ++c) if (std::abs(d[f * ch + c]) > 900) return true;
				return false;
			};
			unsigned a = 0, b = w.frameCount;
			while (a < b && !loud(a)) ++a;
			while (b > a && !loud(b - 1)) --b;
			unsigned pad = w.sampleRate / 50; // keep 20 ms either side
			a = a > pad ? a - pad : 0;
			b = std::min(w.frameCount, b + pad);
			if (b > a + 10) WaveCrop(&w, (int)a, (int)b);
		}

		Sound tone(float freq, float seconds, float volume) {
			int rate = 44100;
			int frames = (int)(rate * seconds);
			short* data = (short*)MemAlloc(frames * sizeof(short));
			for (int i = 0; i < frames; ++i) {
				float t = (float)i / rate;
				float env = std::min(1.0f, t * 200.0f) * std::min(1.0f, (seconds - t) * 30.0f);
				data[i] = (short)(std::sin(2.0f * PI * freq * t) * 32767.0f * volume * env);
			}
			Wave w = { (unsigned)frames, (unsigned)rate, 16, 1, data };
			Sound s = LoadSoundFromWave(w);
			UnloadWave(w);
			return s;
		}
	}

	void Voice::load(const std::string& dir, const std::string& humanDir) {
		if (loaded_ || !IsAudioDeviceReady()) return;
		std::vector<std::string> keys = allNoteWords();
		for (const char* extra : { "ready", "go", "goodluck", "finished" }) keys.push_back(extra);
		int count = 0, human = 0;
		for (const std::string& k : keys) {
			// A recorded co-driver (any of these formats) wins over the TTS clip.
			std::string path;
			bool isHuman = false;
			for (const char* ext : { ".wav", ".ogg", ".mp3", ".flac" }) {
				std::string p = humanDir + "/" + k + ext;
				if (!humanDir.empty() && FileExists(p.c_str())) { path = p; isHuman = true; break; }
			}
			if (path.empty()) path = dir + "/" + k + ".wav";
			if (!FileExists(path.c_str())) continue;
			Wave w = LoadWave(path.c_str());
			if (w.data == nullptr) continue;
			WaveFormat(&w, w.sampleRate, 16, w.channels);
			trimSilence(w);
			clips_[k] = LoadSoundFromWave(w);
			humanClip_[k] = isHuman;
			UnloadWave(w);
			++count;
			if (isHuman) ++human;
		}
		if (human > 0) log::info("voice: %d recorded co-driver clips from %s", human, humanDir.c_str());
		beepLow_ = tone(880.0f, 0.16f, 0.5f);
		beepHigh_ = tone(1320.0f, 0.5f, 0.55f);
		loaded_ = true;
		log::info("voice: loaded %d clips", count);
	}

	void Voice::unload() {
		if (!loaded_) return;
		for (auto& kv : clips_) UnloadSound(kv.second);
		clips_.clear();
		UnloadSound(beepLow_);
		UnloadSound(beepHigh_);
		loaded_ = false;
	}

	void Voice::say(const std::vector<std::string>& words, bool interrupt) {
		if (interrupt) clear();
		for (const auto& w : words) queue_.push_back(w);
	}

	void Voice::clear() {
		queue_.clear();
		if (current_) StopSound(*current_);
		current_ = nullptr;
		playing_ = false;
	}

	void Voice::update(float dt) {
		if (playing_) {
			if (current_ && IsSoundPlaying(*current_)) return;
			playing_ = false;
			current_ = nullptr;
			gap_ = 0.02f;
		}
		if (gap_ > 0.0f) { gap_ -= dt; return; }
		while (!queue_.empty()) {
			std::string w = queue_.front();
			queue_.pop_front();
			auto it = clips_.find(w);
			if (it == clips_.end()) continue;
			current_ = &it->second;
			SetSoundVolume(*current_, volume_);
			SetSoundPitch(*current_, humanClip_[w] ? 1.0f : pitch_);   // TTS is sped up a touch; people aren't
			PlaySound(*current_);
			playing_ = true;
			return;
		}
	}

	void Voice::beep(bool high) {
		if (!loaded_) return;
		Sound& s = high ? beepHigh_ : beepLow_;
		SetSoundVolume(s, volume_ * 0.8f);
		PlaySound(s);
	}
}
