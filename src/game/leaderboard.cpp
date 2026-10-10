#include "game/leaderboard.h"
#include "core/log.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace rally {
	void Leaderboard::load(const std::string& path) {
		path_ = path;
		table_.clear();
		std::ifstream in(path);
		if (!in) return;
		try {
			nlohmann::json j = nlohmann::json::parse(in);
			for (auto& [stage, arr] : j.items()) {
				auto& v = table_[stage];
				for (auto& r : arr) {
					RunRecord rec;
					rec.time = r.value("time", 0.0);
					rec.penalties = r.value("penalties", 0.0);
					rec.resets = r.value("resets", 0);
					rec.driver = r.value("driver", "");
					rec.navigator = r.value("navigator", "");
					rec.car = r.value("car", "");
					rec.date = r.value("date", "");
					if (r.contains("splits")) rec.splits = r["splits"].get<std::vector<double>>();
					v.push_back(rec);
				}
			}
		}
		catch (const std::exception& e) {
			log::warn("leaderboard: could not parse %s: %s", path.c_str(), e.what());
		}
	}

	void Leaderboard::save() const {
		nlohmann::json j = nlohmann::json::object();
		for (auto& [stage, v] : table_) {
			auto arr = nlohmann::json::array();
			for (auto& r : v)
				arr.push_back({ {"time", r.time}, {"penalties", r.penalties}, {"resets", r.resets}, {"driver", r.driver},
					{"navigator", r.navigator}, {"car", r.car}, {"date", r.date}, {"splits", r.splits} });
			j[stage] = arr;
		}
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
		// Atomic-ish write: temp file then rename (same approach as Prairie's saves).
		std::string tmp = path_ + ".tmp";
		{
			std::ofstream out(tmp);
			if (!out) return;
			out << j.dump(1);
		}
		std::filesystem::rename(tmp, path_, ec);
		if (ec) log::warn("leaderboard: save failed: %s", ec.message().c_str());
	}

	int Leaderboard::add(const std::string& stageId, const RunRecord& r) {
		auto& v = table_[stageId];
		v.push_back(r);
		std::stable_sort(v.begin(), v.end(), [](const RunRecord& a, const RunRecord& b) { return a.time < b.time; });
		int rank = 0;
		for (int i = 0; i < (int)v.size(); ++i)
			if (&v[i] != nullptr && v[i].time == r.time && v[i].date == r.date && v[i].driver == r.driver) { rank = i + 1; break; }
		if (v.size() > 10) v.resize(10);
		if (rank > 10) rank = 0;
		save();
		return rank;
	}

	const std::vector<RunRecord>& Leaderboard::top(const std::string& stageId) const {
		static const std::vector<RunRecord> empty;
		auto it = table_.find(stageId);
		return it == table_.end() ? empty : it->second;
	}

	std::vector<double> Leaderboard::bestSplits(const std::string& stageId) const {
		const auto& v = top(stageId);
		return v.empty() ? std::vector<double>{} : v.front().splits;
	}
}
