#include "game/sfx.h"
#include "core/log.h"
#include <algorithm>
#include <filesystem>

namespace rally {
	namespace {
		bool isAudio(const std::filesystem::path& p) {
			std::string ext = p.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac";
		}
		std::string lower(std::string s) {
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return s;
		}
		// "click_2" -> "click"
		std::string baseName(std::string stem) {
			size_t us = stem.find_last_of('_');
			if (us != std::string::npos && us + 1 < stem.size() && std::all_of(stem.begin() + us + 1, stem.end(), ::isdigit)) stem.resize(us);
			return stem;
		}
		std::vector<std::filesystem::path> listAudio(const std::string& dir) {
			std::vector<std::filesystem::path> out;
			std::error_code ec;
			if (!std::filesystem::is_directory(dir, ec)) return out;
			for (auto& e : std::filesystem::directory_iterator(dir, ec)) if (isAudio(e.path())) out.push_back(e.path());
			std::sort(out.begin(), out.end());
			return out;
		}
	}

	void Sfx::load(const std::string& root) {
		if (loaded_ || !IsAudioDeviceReady()) return;
		int n = 0;
		for (const char* sub : { "/ui", "/cues" })
			for (auto& p : listAudio(root + sub)) {
				Sound s = LoadSound(p.string().c_str());
				if (s.frameCount == 0) { log::warn("sfx: could not load %s", p.string().c_str()); continue; }
				sounds_[baseName(lower(p.stem().string()))].push_back(s);
				++n;
			}
		for (auto& p : listAudio(root + "/ambience")) {
			Music m = LoadMusicStream(p.string().c_str());
			if (m.frameCount == 0) { log::warn("sfx: could not load %s", p.string().c_str()); continue; }
			m.looping = true;
			loops_[lower(p.stem().string())].m = m;
			++n;
		}
		for (auto& p : listAudio(root + "/music")) {
			Music m = LoadMusicStream(p.string().c_str());
			if (m.frameCount == 0) { log::warn("sfx: could not load %s", p.string().c_str()); continue; }
			tracks_.push_back(m);
		}
		// With one track it simply loops; with several they play in turn.
		for (Music& m : tracks_) m.looping = tracks_.size() == 1;
		log::info("sfx: %d sounds/loops, %d music tracks", n, (int)tracks_.size());
		loaded_ = true;
	}

	void Sfx::unload() {
		if (!loaded_) return;
		for (auto& kv : sounds_) for (Sound& s : kv.second) UnloadSound(s);
		for (auto& kv : loops_) UnloadMusicStream(kv.second.m);
		for (Music& m : tracks_) UnloadMusicStream(m);
		sounds_.clear(); loops_.clear(); tracks_.clear();
		loaded_ = false;
	}

	bool Sfx::has(const std::string& name) const {
		auto it = sounds_.find(name);
		return it != sounds_.end() && !it->second.empty();
	}

	void Sfx::play(const std::string& name, float volume, float pitchVar) {
		auto it = sounds_.find(name);
		if (it == sounds_.end() || it->second.empty()) return;
		Sound& s = it->second[GetRandomValue(0, (int)it->second.size() - 1)];
		SetSoundVolume(s, volume);
		SetSoundPitch(s, 1.0f + pitchVar * (GetRandomValue(-100, 100) / 100.0f));
		PlaySound(s);
	}

	void Sfx::setAmbience(const std::string& name, float volume) { ambName_ = name; ambVolume_ = volume; }
	void Sfx::setCrowd(float volume) { crowdVolume_ = volume; }

	void Sfx::update(float dt) {
		if (!loaded_) return;
		for (auto& kv : loops_) {
			Loop& l = kv.second;
			float target = kv.first == "crowd" ? crowdVolume_ : (kv.first == ambName_ ? ambVolume_ : 0.0f);
			float step = dt * 0.8f;   // ~1.2 s crossfades
			l.vol = l.vol < target ? std::min(target, l.vol + step) : std::max(target, l.vol - step);
			if (l.vol > 0.001f) {
				if (!l.playing) { PlayMusicStream(l.m); l.playing = true; }
				SetMusicVolume(l.m, l.vol);
				UpdateMusicStream(l.m);
			}
			else if (l.playing) { StopMusicStream(l.m); l.playing = false; }
		}
		if (!tracks_.empty()) {
			// Keeps streaming at volume 0 (in a stage) so it resumes where it was.
			Music& m = tracks_[track_];
			if (!musicPlaying_) { PlayMusicStream(m); musicPlaying_ = true; }
			SetMusicVolume(m, musicVolume_);
			UpdateMusicStream(m);
			if (tracks_.size() > 1 && !IsMusicStreamPlaying(m)) {
				track_ = (track_ + 1) % (int)tracks_.size();
				musicPlaying_ = false;
			}
		}
	}
}
