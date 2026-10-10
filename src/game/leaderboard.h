#pragma once
#include <map>
#include <string>
#include <vector>

namespace rally {
	struct RunRecord {
		double time = 0;          // final time incl. penalties
		double penalties = 0;
		int resets = 0;
		std::string driver, navigator, car, date;
		std::vector<double> splits;
	};

	// Local per-stage top-10 table, saved to saves/leaderboard.json.
	class Leaderboard {
	public:
		void load(const std::string& path);
		void save() const;
		// Inserts a run, returns its 1-based rank (0 = didn't make the table).
		int add(const std::string& stageId, const RunRecord& r);
		const std::vector<RunRecord>& top(const std::string& stageId) const;
		// Best run's split times, empty if none.
		std::vector<double> bestSplits(const std::string& stageId) const;

	private:
		std::string path_;
		std::map<std::string, std::vector<RunRecord>> table_;
	};
}
