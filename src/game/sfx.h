#pragma once
#include <raylib.h>
#include <map>
#include <string>
#include <vector>

namespace rally {
	// Recorded sounds played from the game thread: menu blips, stage cues,
	// ambience loops and the music playlist. Every slot is optional - a
	// missing file just means silence (or the built-in fallback). See ASSETS.md.
	class Sfx {
	public:
		void load(const std::string& root);   // e.g. "res/audio"
		void unload();

		// One-shots from ui/ and cues/: "click", "move", "back", "beep", "go",
		// "split", "finish", "penalty", "cheer"... Variants: name_1, name_2, ...
		bool has(const std::string& name) const;
		void play(const std::string& name, float volume = 1.0f, float pitchVar = 0.0f);

		// Ambience loop by name ("day", "night", "snow", "rain"), "" for none.
		// Crossfades between loops.
		void setAmbience(const std::string& name, float volume);
		bool hasLoop(const std::string& name) const { return loops_.count(name) > 0; }
		void setCrowd(float volume);          // crowd loop level, 0 = off

		// Music playlist (every track in music/, played in turn).
		void setMusicVolume(float v) { musicVolume_ = v; }
		void update(float dt);

	private:
		struct Loop { Music m{}; float vol = 0.0f; bool playing = false; };
		std::map<std::string, std::vector<Sound>> sounds_;
		std::map<std::string, Loop> loops_;    // ambience/*, incl. "crowd"
		std::string ambName_;
		float ambVolume_ = 0.0f, crowdVolume_ = 0.0f;
		std::vector<Music> tracks_;
		int track_ = 0;
		float musicVolume_ = 0.0f;
		bool musicPlaying_ = false;
		bool loaded_ = false;
	};
}
