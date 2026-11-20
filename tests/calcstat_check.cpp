// Runs the old CalcStat (taken from git by calcstat_check.sh) and the new one
// on the same stats for every input, and compares all players afterwards.
// Five differences are intended, fixes of slips in single copies of the old
// code; any other difference fails.

#include "win_nix.h"
#include "objects.h"
#include <map>
#include <string>

std::vector<PlayerStat> PS;

#include "old_calcstat.inc"

static const int TRACKS[] = { 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008,
	1102, 1103, 1104, 1105, 1106, 1107, 1108, 1109,
	1201, 1202, 1206, 1207, 1210, 1214,
	1301, 1302, 1303, 1304, 1305, 1306, 1307, 1308, 0, 1099, 9999 };

static PlayerStat Player(const char* name, int rep, int wins, int loses, int rating, int oppsRep) {
	PlayerStat p;
	memset(&p, 0, sizeof(p));
	snprintf(p.Name, sizeof(p.Name), "%s", name);
	int* v = &p.Rating_All;
	for (int m = 0; m < 5; m++) {
		v[m * 7 + 0] = rating;
		v[m * 7 + 1] = wins;
		v[m * 7 + 2] = loses;
		v[m * 7 + 3] = loses / 2;
		v[m * 7 + 4] = rep;
		v[m * 7 + 5] = oppsRep;
		v[m * 7 + 6] = rating + 1;
	}
	return p;
}

// Which of the reporter's 35 numbers differ (bit i), or -1 if anybody else
// differs.
static long long Diff(std::vector<PlayerStat> a, std::vector<PlayerStat> b) {
	long long bits = 0;
	for (size_t i = 0; i < a.size(); i++) {
		std::vector<PlayerStat>::iterator it = std::find(b.begin(), b.end(), a[i].Name);
		if (it == b.end()) return -1;
		int* x = &a[i].Rating_All;
		int* y = &it->Rating_All;
		for (int f = 0; f < 35; f++)
			if (x[f] != y[f]) {
				if (strcmp(a[i].Name, "R") != 0) return -1;
				bits |= 1LL << f;
			}
	}
	return bits;
}

static long long F(int mode, int field) { return 1LL << (mode * 7 + field); }
enum { RATING, WINS, LOSES, DISC, REP, OPPS_REP, OPPS_RATING };

// The slip of the old code an input runs into, if the difference stays within
// the numbers that slip touches; NULL otherwise. Positions (Rating) may move
// with REP.
static const char* Fixed(int count, int type, int laps, int place, int disc, int startRep, int oppsRep, long long bits) {
	int m = type + 1;
	long long rep = F(0, REP) | F(m, REP) | F(0, RATING) | F(m, RATING);
	if (count == 2 && type == 1 && place == 1 && startRep > 99000000 && !(bits & ~rep))
		return "2-player sprint win: REP cap checked the circuit REP";
	if (count == 4 && type == 1 && place == 1 && !(bits & ~rep))
		return "4-player sprint win: extra rounding";
	if (type == 3 && laps == 7 && place == 1 && oppsRep >= 100000 && !(bits & ~rep))
		return "7-lap drift win: fixed amounts of 3 laps";
	if (count == 4 && type == 0 && laps == 6 && place == 3 && disc == -1 && !(bits & ~(F(0, DISC) | F(m, DISC))))
		return "3rd of 4, 6-lap circuit: disconnect not counted";
	if (count >= 3 && type == 0 && laps == 2 && !(bits & ~(F(0, OPPS_REP) | F(0, OPPS_RATING))))
		return "3-4 player 2-lap circuit: overall opponent average";
	return NULL;
}

int main() {
	const int reps[] = { 100, 1234, 150000, 250000, 99999990 };
	const int records[][2] = { { 0, 0 }, { 7, 4 } };
	const int opps[][3] = { { 100, 100, 100 }, { 45000, 30000, 20000 }, { 120000, 5000, 80000 }, { 150000, 150000, 150000 } };
	long runs = 0, unexpected = 0;
	std::map<std::string, long> fixed;

	for (int r = 0; r < 5; r++) for (int w = 0; w < 2; w++) for (int o = 0; o < 5; o++) {
		std::vector<PlayerStat> start;
		start.push_back(Player("R", reps[r], records[w][0], records[w][1], 4, 3000));
		for (int i = 0; i < 3; i++) {
			if (o == 4 && i == 1) continue;  // an opponent without stats
			start.push_back(Player(i == 0 ? "O1" : i == 1 ? "O2" : "O3", opps[o == 4 ? 1 : o][i], 3 + i, 2, 2 + i, 900));
		}
		start.push_back(Player("B1", 500, 1, 9, 9, 100));
		start.push_back(Player("B2", 60000, 20, 3, 1, 70000));
		start.push_back(Player("B3", 3000000, 50, 50, 2, 200000));

		for (int count = 2; count <= 4; count++)
		for (int type = 0; type <= 3; type++)
		for (int laps = 0; laps <= 11; laps++)
		for (int place = 0; place <= count + 1; place++)
		for (size_t t = 0; t < sizeof(TRACKS) / sizeof(TRACKS[0]); t++)
		for (int disc = -1; disc <= 0; disc++) {
			PS = start;
			CalcStatOld("R", "O1", count > 2 ? "O2" : NULL, count > 3 ? "O3" : NULL, count, type, TRACKS[t], laps, place, disc);
			std::vector<PlayerStat> before = PS;
			PS = start;
			CalcStat("R", "O1", count > 2 ? "O2" : NULL, count > 3 ? "O3" : NULL, count, type, TRACKS[t], laps, place, disc);
			runs++;
			long long bits = Diff(before, PS);
			if (bits == 0) continue;
			// the opponents' REP as CalcStat adds it up: a missing one repeats the one before
			int oppsRep = 0, last = 0;
			for (int i = 0; i < count - 1; i++) {
				if (!(o == 4 && i == 1)) last = opps[o == 4 ? 1 : o][i];
				oppsRep += last;
			}
			const char* why = bits < 0 ? NULL : Fixed(count, type, laps, place, disc, reps[r], oppsRep, bits);
			if (why != NULL) {
				fixed[why]++;
				continue;
			}
			if (unexpected++ < 10)
				printf("DIFFERENT: rep %d record %d opps %d | count %d type %d laps %d place %d track %d disc %d\n",
					reps[r], w, o, count, type, laps, place, TRACKS[t], disc);
		}
	}
	printf("%ld inputs compared\n", runs);
	for (std::map<std::string, long>::iterator it = fixed.begin(); it != fixed.end(); ++it)
		printf("  %6ld differ as intended: %s\n", it->second, it->first.c_str());
	if (unexpected) {
		printf("%ld inputs differ unexpectedly\n", unexpected);
		return 1;
	}
	printf("every other input gives the same stats\n");
	return 0;
}
