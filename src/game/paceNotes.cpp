#include "game/paceNotes.h"
#include "game/stage.h"
#include <cmath>
#include <algorithm>

namespace rally {
	const char* modName(uint32_t bit) {
		switch (bit) {
		case MOD_LONG: return "long";
		case MOD_TIGHTENS: return "tightens";
		case MOD_OPENS: return "opens";
		case MOD_DONTCUT: return "don't cut";
		case MOD_CAUTION: return "caution";
		case MOD_OVERCREST: return "over crest";
		case MOD_SQUARE: return "square";
		default: return "";
		}
	}

	namespace {
		int severityForRadius(float r) {
			if (r <= 14.0f) return 0;
			if (r <= 24.0f) return 1;
			if (r <= 36.0f) return 2;
			if (r <= 52.0f) return 3;
			if (r <= 76.0f) return 4;
			if (r <= 112.0f) return 5;
			return 6;
		}

		std::string linkFor(float gap) {
			if (gap < 18.0f) return "into";
			if (gap < 42.0f) return "and";
			static const int d[] = { 50, 70, 100, 150, 200, 250, 300 };
			int best = 50;
			for (int v : d) if (gap >= v - 10) best = v;
			return std::to_string(best);
		}
	}

	std::vector<PaceNote> generatePaceNotes(const Stage& stage) {
		std::vector<PaceNote> notes;
		const auto& S = stage.samples;
		int N = (int)S.size();
		if (N < 50) return notes;

		// Extra smoothing on curvature.
		std::vector<float> k(N);
		for (int i = 0; i < N; ++i) {
			int a = std::max(0, i - 5), b = std::min(N - 1, i + 5);
			float sum = 0;
			for (int j = a; j <= b; ++j) sum += S[j].curvature;
			k[i] = sum / (b - a + 1);
		}

		// --- Corners: runs of same-sign curvature above threshold ---
		const float kMin = 1.0f / 190.0f;
		struct Run { int a, b, sign; };
		std::vector<Run> runs;
		for (int i = 0; i < N; ++i) {
			if (std::fabs(k[i]) < kMin) continue;
			int sign = k[i] > 0 ? 1 : -1;
			int j = i;
			while (j + 1 < N && std::fabs(k[j + 1]) >= kMin && (k[j + 1] > 0 ? 1 : -1) == sign) ++j;
			if (!runs.empty() && runs.back().sign == sign && i - runs.back().b < 10) runs.back().b = j;
			else runs.push_back({ i, j, sign });
			i = j;
		}

		float finishS = stage.finishS;
		float lastEnd = 0;
		for (const Run& r : runs) {
			float sA = S[r.a].s, sB = S[r.b].s;
			if (sA > finishS || sB < stage.startS - 10) continue;
			float maxK = 0, angle = 0;
			for (int i = r.a; i <= r.b; ++i) { maxK = std::max(maxK, std::fabs(k[i])); angle += k[i]; }
			angle = std::fabs(angle);
			float len = sB - sA;
			if (len < 5.0f || angle < 0.2f) continue;
			int sev = severityForRadius(1.0f / maxK);
			if (sev == 6 && angle < 0.32f) continue;

			PaceNote n;
			n.type = NoteType::Corner;
			n.s = sA;
			n.sEnd = sB;
			// +curvature = heading turning toward +x = car's left.
			n.direction = r.sign > 0 ? -1 : 1;
			n.severity = sev;
			if (angle > 1.25f && angle < 1.95f && len < 32.0f && sev >= 1 && sev <= 2) n.mods |= MOD_SQUARE;
			if (sev != 0 && (angle > 2.2f || len > 95.0f)) n.mods |= MOD_LONG;
			int third = std::max(1, (r.b - r.a) / 3);
			float kFirst = 0, kLast = 0;
			for (int i = r.a; i < r.a + third; ++i) kFirst = std::max(kFirst, std::fabs(k[i]));
			for (int i = r.b - third; i <= r.b; ++i) kLast = std::max(kLast, std::fabs(k[i]));
			if (len > 25.0f && sev != 0) {
				if (kLast > kFirst * 1.5f) n.mods |= MOD_TIGHTENS;
				else if (kFirst > kLast * 1.5f) n.mods |= MOD_OPENS;
			}
			for (const Vector2& dc : stage.dontCut) {
				int insideSide = n.direction; // inside of a left corner is the left side
				if (dc.x >= sA - 8 && dc.x <= sB + 8 && (int)dc.y == insideSide) n.mods |= MOD_DONTCUT;
			}
			if (sev == 0 || (sev <= 2 && sA - lastEnd > 160.0f)) n.mods |= MOD_CAUTION;
			for (float sp : stage.spectatorS) if (sp >= sA - 30 && sp <= sB + 30) n.mods |= MOD_CAUTION;
			notes.push_back(n);
			lastEnd = sB;
		}

		// --- Crests from the elevation profile ---
		for (int i = 30; i < N - 30; ++i) {
			float y = S[i].pos.y;
			float d2 = (S[i - 25].pos.y + S[i + 25].pos.y - 2 * y) / (25.0f * 25.0f);
			if (d2 > -0.0035f) continue;
			if (y < S[i - 1].pos.y || y < S[i + 1].pos.y) continue; // local max only
			if (S[i].s < stage.startS || S[i].s > finishS) continue;
			bool nearJump = false;
			for (auto& j : stage.def.jumps) if (std::fabs(j.at - S[i].s) < 30) nearJump = true;
			if (nearJump) { i += 30; continue; }
			// Merge into a corner that contains it / starts right after it.
			bool merged = false;
			for (auto& n : notes) {
				if (n.type == NoteType::Corner && S[i].s >= n.s - 18 && S[i].s <= n.sEnd) { n.mods |= MOD_OVERCREST; merged = true; break; }
			}
			if (!merged) {
				PaceNote c; c.type = NoteType::Crest; c.s = c.sEnd = S[i].s - 6.0f; c.direction = 0;
				notes.push_back(c);
			}
			i += 30;
		}
		for (auto& j : stage.def.jumps) {
			if (j.at < stage.startS || j.at > finishS) continue;
			PaceNote n; n.type = NoteType::Jump; n.s = n.sEnd = j.at - 10.0f;
			if (j.height > 1.3f) n.mods |= MOD_CAUTION;
			notes.push_back(n);
		}
		for (auto& b : stage.def.bridges) {
			if (b.from < stage.startS || b.from > finishS) continue;
			PaceNote n; n.type = NoteType::Bridge; n.s = b.from - 6.0f; n.sEnd = b.to;
			notes.push_back(n);
		}
		PaceNote fin; fin.type = NoteType::Finish; fin.s = fin.sEnd = finishS;
		notes.push_back(fin);

		std::sort(notes.begin(), notes.end(), [](const PaceNote& a, const PaceNote& b) { return a.s < b.s; });

		// --- Manual overrides from the stage file ---
		for (const NoteOverride& o : stage.def.noteOverrides) {
			int best = -1; float bd = 25.0f;
			for (int i = 0; i < (int)notes.size(); ++i) {
				float d = std::fabs(notes[i].s - o.at);
				if (d < bd) { bd = d; best = i; }
			}
			if (best < 0) continue;
			PaceNote& n = notes[best];
			if (o.remove) { n.index = -1; continue; }
			if (o.severity >= 0) n.severity = std::clamp(o.severity, 0, 6);
			if (o.direction != 0) n.direction = o.direction;
			n.mods = (n.mods | o.modsSet) & ~o.modsClear;
		}
		notes.erase(std::remove_if(notes.begin(), notes.end(), [](const PaceNote& n) { return n.index == -1; }), notes.end());

		for (int i = 0; i < (int)notes.size(); ++i) {
			notes[i].index = i;
			if (i + 1 < (int)notes.size() && notes[i].type != NoteType::Finish)
				notes[i].link = linkFor(notes[i + 1].s - notes[i].sEnd);
		}
		return notes;
	}

	std::string noteText(const PaceNote& n, bool withLink) {
		std::string t;
		switch (n.type) {
		case NoteType::Corner:
			if (n.severity == 0) t = std::string(n.direction < 0 ? "L" : "R") + " hairpin";
			else if (n.mods & MOD_SQUARE) t = std::string(n.direction < 0 ? "L" : "R") + " square";
			else t = std::string(n.direction < 0 ? "L" : "R") + std::to_string(n.severity);
			for (uint32_t b = 1; b < (1u << MOD_COUNT); b <<= 1)
				if ((n.mods & b) && b != MOD_SQUARE) { t += " "; t += modName(b); }
			break;
		case NoteType::Crest: t = "crest"; break;
		case NoteType::Jump: t = (n.mods & MOD_CAUTION) ? "jump caution" : "jump"; break;
		case NoteType::Bridge: t = "over bridge"; break;
		case NoteType::Finish: t = "finish"; break;
		}
		if (withLink && !n.link.empty()) t += ", " + n.link;
		return t;
	}

	std::vector<std::string> noteWords(const PaceNote& n) {
		std::vector<std::string> w;
		switch (n.type) {
		case NoteType::Corner:
			if (n.severity == 0) { w.push_back("hairpin"); w.push_back(n.direction < 0 ? "left" : "right"); }
			else if (n.mods & MOD_SQUARE) { w.push_back(n.direction < 0 ? "left" : "right"); w.push_back("square"); }
			else { w.push_back(n.direction < 0 ? "left" : "right"); w.push_back(std::to_string(n.severity)); }
			if (n.mods & MOD_OVERCREST) w.push_back("overcrest");
			if (n.mods & MOD_LONG) w.push_back("long");
			if (n.mods & MOD_TIGHTENS) w.push_back("tightens");
			if (n.mods & MOD_OPENS) w.push_back("opens");
			if (n.mods & MOD_DONTCUT) w.push_back("dontcut");
			if (n.mods & MOD_CAUTION) w.push_back("caution");
			break;
		case NoteType::Crest: w.push_back("crest"); break;
		case NoteType::Jump: w.push_back("jump"); if (n.mods & MOD_CAUTION) w.push_back("caution"); break;
		case NoteType::Bridge: w.push_back("overbridge"); break;
		case NoteType::Finish: w.push_back("finish"); break;
		}
		if (!n.link.empty()) w.push_back(n.link);
		return w;
	}

	std::vector<std::string> allNoteWords() {
		return { "left", "right", "1", "2", "3", "4", "5", "6", "hairpin", "square", "long", "tightens", "opens",
			"dontcut", "caution", "overcrest", "crest", "jump", "overbridge", "finish", "into", "and",
			"50", "70", "100", "150", "200", "250", "300" };
	}
}
