#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace rally {
	class Stage;

	enum NoteMod : uint32_t {
		MOD_LONG = 1 << 0,
		MOD_TIGHTENS = 1 << 1,
		MOD_OPENS = 1 << 2,
		MOD_DONTCUT = 1 << 3,
		MOD_CAUTION = 1 << 4,
		MOD_OVERCREST = 1 << 5,
		MOD_SQUARE = 1 << 6,
		MOD_COUNT = 7
	};
	const char* modName(uint32_t bit);

	enum class NoteType : uint8_t { Corner, Crest, Jump, Bridge, Finish };

	struct PaceNote {
		int index = 0;
		NoteType type = NoteType::Corner;
		float s = 0;          // where the feature starts
		float sEnd = 0;       // where it ends (= s for point features)
		int direction = 0;    // -1 left, +1 right
		int severity = 4;     // 1 (slowest) .. 6 (fastest); 0 = hairpin
		uint32_t mods = 0;
		std::string link;     // "into", "and", "50", ... (distance to the next note)
	};

	// Reads the road geometry/profile and writes rally-style pace notes.
	std::vector<PaceNote> generatePaceNotes(const Stage& stage);

	// "L4 long tightens" / "R hairpin" / "crest"
	std::string noteText(const PaceNote& n, bool withLink = true);
	// Voice clip keys (res/audio/notes/<key>.wav), in speaking order.
	std::vector<std::string> noteWords(const PaceNote& n);
	// All voice keys the generator can ever use (for tools/genvoice).
	std::vector<std::string> allNoteWords();
}
