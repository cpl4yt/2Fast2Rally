#pragma once
#include <raylib.h>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace rally {
	// Co-driver voice: plays short word clips back to back, with leading and
	// trailing silence trimmed so calls sound continuous.
	class Voice {
	public:
		// dir: TTS clips; humanDir: recorded co-driver clips (preferred per word).
		void load(const std::string& dir, const std::string& humanDir = std::string());
		void unload();
		void say(const std::vector<std::string>& words, bool interrupt = false);
		void sayOne(const std::string& word, bool interrupt = false) { say({ word }, interrupt); }
		void update(float dt);
		void clear();
		bool busy() const { return playing_ || !queue_.empty(); }
		void setVolume(float v) { volume_ = v; }
		void setPitch(float p) { pitch_ = p; }

		// Countdown beeps.
		void beep(bool high);

	private:
		std::unordered_map<std::string, Sound> clips_;
		std::unordered_map<std::string, bool> humanClip_;
		std::deque<std::string> queue_;
		Sound* current_ = nullptr;
		bool playing_ = false;
		float gap_ = 0.0f;
		float volume_ = 1.0f;
		float pitch_ = 1.06f;
		Sound beepLow_{}, beepHigh_{};
		bool loaded_ = false;
	};
}
