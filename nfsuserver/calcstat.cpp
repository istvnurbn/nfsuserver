// Ratings after a race. One race report updates the reporter's stats: wins,
// losses, disconnects and REP for the race type and for "all", their positions
// (Rating) and the running averages of their opponents' REP and rating.

#include "win_nix.h"
#include "objects.h"

extern std::vector<PlayerStat> PS;

// The numbers of a PlayerStat, 7 per mode: all, circuit, sprint, drag, drift.
enum { RATING, WINS, LOSES, DISC, REP, OPPS_REP, OPPS_RATING };
static int* Stats(PlayerStat& ps, int mode) {
	return &ps.Rating_All + 7 * mode;
}

// most REP first
struct ByRep {
	int mode;
	ByRep(int m) : mode(m) {}
	bool operator()(PlayerStat a, PlayerStat b) const { return Stats(a, mode)[REP] > Stats(b, mode)[REP]; }
};
bool sort_REP_All(PlayerStat a, PlayerStat b) { return ByRep(0)(a, b); }
bool sort_REP_Circ(PlayerStat a, PlayerStat b) { return ByRep(1)(a, b); }
bool sort_REP_Sprint(PlayerStat a, PlayerStat b) { return ByRep(2)(a, b); }
bool sort_REP_Drag(PlayerStat a, PlayerStat b) { return ByRep(3)(a, b); }
bool sort_REP_Drift(PlayerStat a, PlayerStat b) { return ByRep(4)(a, b); }

// race_type 0-3: circuit, sprint, drag, drift. An unknown name leaves rep and
// rating as they were.
void GetOppREP_Rating(const char* name, const char race_type, int& rep, int& rating) {
	std::vector<PlayerStat>::iterator it = std::find(PS.begin(), PS.end(), name);
	if (it == PS.end() || race_type < 0 || race_type > 3) return;
	rep = Stats(*it, race_type + 1)[REP];
	rating = Stats(*it, race_type + 1)[RATING];
}

// A win pays 10% of the opponents' REP times this, or this times 10,000 once
// they have 100,000 REP or more. Circuit and drift: one row per group of
// tracks, one column per lap count from 2 to 10.
static const double CIRCUIT[4][9] = {
	{ 1.0, 1.1, 1.2, 1.7, 2.2, 2.7, 3.2, 3.7, 4.2 },	// other tracks
	{ 1.3, 1.4, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5 },	// 1001 Market Street, 1002 Stadium
	{ 1.1, 1.2, 1.3, 1.8, 2.3, 2.8, 3.3, 3.8, 4.3 },	// 1003 Olympic Square
	{ 1.2, 1.3, 1.4, 1.9, 2.4, 2.9, 3.4, 3.9, 4.4 },	// 1004 Terminal, 1005 Atlantica
};
static const double DRIFT[5][9] = {
	{ 1.3, 1.4, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5 },	// other tracks
	{ 1.0, 1.1, 1.2, 1.7, 2.2, 2.7, 3.2, 3.7, 4.2 },	// 1301
	{ 1.1, 1.2, 1.3, 1.8, 2.3, 2.8, 3.3, 3.8, 4.3 },	// 1302, 1303
	{ 1.2, 1.3, 1.4, 1.9, 2.4, 2.9, 3.4, 3.9, 4.4 },	// 1304
	{ 1.4, 1.5, 1.6, 2.1, 2.6, 3.1, 3.6, 4.1, 4.6 },	// 1306
};

static double Multiplier(int type, int track, int laps) {
	switch (type) {
	case 0:
		return CIRCUIT[(track == 1001 || track == 1002) ? 1 : track == 1003 ? 2 : (track == 1004 || track == 1005) ? 3 : 0][laps - 2];
	case 1:	// sprint: one lap
		return track == 1102 ? 1.0 : (track >= 1104 && track <= 1106) ? 1.2 : track == 1107 ? 1.4 : 1.1;
	case 2:	// drag
		return 1.0;
	default:
		return DRIFT[track == 1301 ? 1 : (track == 1302 || track == 1303) ? 2 : track == 1304 ? 3 : track == 1306 ? 4 : 0][laps - 2];
	}
}

// The share of their REP a losing place costs: last 10%, 3rd of 4 5%, the
// other places nothing (they still count as a loss).
static double LossShare(int count, int place) {
	if (place == count) return 0.1;
	if (count == 4 && place == 3) return 0.05;
	return 0;
}

// The running average of the opponents, over the races of one mode.
static void Averages(int mode, const char* reporter, int opps_rep, int opps_rating) {
	std::sort(PS.begin(), PS.end(), ByRep(mode));
	std::vector<PlayerStat>::iterator tps = std::find(PS.begin(), PS.end(), reporter);
	int* s = Stats(*tps, mode);
	int races = s[WINS] + s[LOSES];
	s[RATING] = std::distance(PS.begin(), tps) + 1;
	s[OPPS_REP] = (s[OPPS_REP] * (races - 1) + opps_rep) / races;
	s[OPPS_RATING] = (s[OPPS_RATING] * (races - 1) + opps_rating) / races;
}

void CalcStat(const char* reporter, const char* opp1, const char* opp2, const char* opp3,
	const int count, const int type, const int track, const int laps, const int place, const int disc) {

	// a place outside the race divides by zero below, a reporter without stats
	// is written through PS.end()
	if (place < 1 || place > count || std::find(PS.begin(), PS.end(), reporter) == PS.end()) return;
	if (count < 2 || count > 4 || type < 0 || type > 3) return;
	if ((type == 0 || type == 3) && (laps < 2 || laps > 10)) return;

	// the opponents' REP added up and their rating averaged
	const char* opps[3] = { opp1, opp2, opp3 };
	int tmp_rep = 0, tmp_rating = 0, opps_rep = 0, opps_rating = 0;
	for (int i = 0; i < count - 1; i++) {
		if (opps[i] != NULL) GetOppREP_Rating(opps[i], type, tmp_rep, tmp_rating);
		opps_rep += tmp_rep;
		opps_rating += tmp_rating;
	}
	opps_rating /= count - 1;

	int mode = type + 1;
	PlayerStat& ps = *std::find(PS.begin(), PS.end(), reporter);
	int* all = Stats(ps, 0);
	int* m = Stats(ps, mode);

	if (place == 1) {
		double mult = Multiplier(type, track, laps);
		int win_rep = opps_rep < 100000 ? (int)floor((opps_rep * mult * 0.1) + 0.5) : (int)(mult * 10000 + 0.5);
		all[WINS]++;
		m[WINS]++;
		all[REP] = std::min(all[REP] + win_rep / 4, 99999999);
		m[REP] = std::min(m[REP] + win_rep, 99999999);
	}
	else {
		double share = LossShare(count, place);
		all[LOSES]++;
		m[LOSES]++;
		if (disc == -1) {
			all[DISC]++;
			m[DISC]++;
		}
		if (share > 0) {
			int lost_rep = m[REP] < 200000 ? (int)floor((m[REP] * share) + 0.5) : 20000;
			all[REP] = std::max(all[REP] - lost_rep / 4, 100);
			m[REP] = std::max(m[REP] - lost_rep, 100);
		}
	}

	Averages(mode, reporter, opps_rep / (count - 1), opps_rating);
	Averages(0, reporter, opps_rep / (count - 1), opps_rating);
}
