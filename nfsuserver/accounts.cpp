#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <map>
#include <string>
#include <crypt.h>
#include <sqlite3.h>
#include "accounts.h"

void Log( char * log );

static sqlite3 * db = NULL;

// Failed logins, kept in memory: a restart forgets them. An account locks after
// 5 wrong passwords, except for the address it last logged in from, so nobody can
// lock a player out; an address is blocked after 20 failed logins on any accounts.
struct Strikes { int count; time_t first, until; };
typedef std::map<std::string, Strikes> StrikeMap;
static StrikeMap accountStrikes, addressStrikes;
static const int WINDOW = 15 * 60, ACCOUNT_LIMIT = 5, ADDRESS_LIMIT = 20;

static bool Blocked( StrikeMap & m, const std::string & key, time_t now ) {
	StrikeMap::iterator it = m.find (key);
	return it != m.end () && it->second.until > now;
}

// count a failure: the limit within WINDOW seconds blocks the key for WINDOW seconds
static void Strike( StrikeMap & m, const std::string & key, int limit, time_t now ) {
	if (m.size () > 10000) // guessing from many addresses must not fill the memory
		for (StrikeMap::iterator it = m.begin (); it != m.end ();)
			if (it->second.until <= now && now - it->second.first > WINDOW) m.erase (it++);
			else ++it;
	Strikes & s = m[key];
	if (now - s.first > WINDOW) {
		s.count = 0;
		s.first = now;
	}
	if (++s.count >= limit) {
		s.until = now + WINDOW;
		s.count = 0;
	}
}

static void DbError( const char * what ) {
	char log[512];
	snprintf (log, sizeof (log), "[error] %s: %s\n", what, sqlite3_errmsg (db));
	Log (log);
}

// One statement, text parameters bound in order. Returns the sqlite3_step result
// of the first row; the caller reads columns from *out and finalizes it.
static int Run( sqlite3_stmt ** out, const char * sql, const char * a = NULL, const char * b = NULL ) {
	sqlite3_stmt * st;
	if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK) {
		DbError (sql);
		return SQLITE_ERROR;
	}
	if (a) sqlite3_bind_text (st, 1, a, -1, SQLITE_TRANSIENT);
	if (b) sqlite3_bind_text (st, 2, b, -1, SQLITE_TRANSIENT);
	int rc = sqlite3_step (st);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) DbError (sql);
	if (out) *out = st;
	else sqlite3_finalize (st);
	return rc;
}

static int Exec( const char * sql, const char * a = NULL, const char * b = NULL ) {
	return Run (NULL, sql, a, b);
}

// rusers.dat: raw RegUser structs, Username[16] + Personas[4][16] + a pointer
// (84 bytes from 32-bit builds, 88 from 64-bit ones). No passwords: the first
// login of an imported account sets it.
static void ImportRusers() {
	FILE * f = fopen ("rusers.dat", "rb");
	if (f == NULL) return;
	fseek (f, 0, SEEK_END);
	long size = ftell (f);
	fseek (f, 0, SEEK_SET);
	size_t rec = (size % 88 == 0) ? 88 : 84;

	char r[88], log[256];
	int accounts = 0;
	while (fread (r, 1, rec, f) == rec) {
		char name[17] = { 0 };
		memcpy (name, r, 16);
		if (name[0] == 0) continue; // every start of the old server appended a blank record
		if (Exec ("INSERT OR IGNORE INTO accounts (name) VALUES (?)", name) != SQLITE_DONE) continue;
		accounts++;
		for (int i = 0; i < 4; i++) {
			char pers[17] = { 0 };
			memcpy (pers, r + 16 + 16 * i, 16);
			if (pers[0] != 0) Exec ("INSERT OR IGNORE INTO personas (name, account) VALUES (?, ?)", pers, name);
		}
	}
	fclose (f);
	snprintf (log, sizeof (log), "Imported %d accounts from rusers.dat\n", accounts);
	Log (log);
}

static bool Upgrade( int version, bool (*schema)() );
static bool Schema1();
static bool Schema2();
static void ImportStats();

bool DbOpen( const char * path ) {
	if (sqlite3_open (path, &db) != SQLITE_OK) {
		DbError (path);
		return false;
	}
	sqlite3_busy_timeout (db, 5000);
	Exec ("PRAGMA foreign_keys = ON");

	sqlite3_stmt * st;
	Run (&st, "PRAGMA user_version");
	int version = sqlite3_column_int (st, 0);
	sqlite3_finalize (st);

	// each schema version and its one-time import in one transaction
	return (version >= 1 || Upgrade (1, Schema1))
		&& (version >= 2 || Upgrade (2, Schema2));
}

static bool Upgrade( int version, bool (*schema)() ) {
	char sql[64];
	snprintf (sql, sizeof (sql), "PRAGMA user_version = %d", version);
	Exec ("BEGIN");
	if (schema () && Exec (sql) == SQLITE_DONE && Exec ("COMMIT") == SQLITE_DONE) return true;
	Exec ("ROLLBACK");
	return false;
}

// accounts and personas, imported from rusers.dat
static bool Schema1() {
	bool ok = Exec (
		"CREATE TABLE accounts ("
		" name TEXT PRIMARY KEY COLLATE NOCASE,"
		" password_hash TEXT," // NULL until the first login of an imported account
		" is_admin INTEGER NOT NULL DEFAULT 0,"
		" banned INTEGER NOT NULL DEFAULT 0,"
		" created TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
		" last_login TEXT,"
		" last_ip TEXT)") == SQLITE_DONE // where the last login came from, never locked out
		&& Exec (
		"CREATE TABLE personas ("
		" name TEXT PRIMARY KEY COLLATE NOCASE,"
		" account TEXT NOT NULL REFERENCES accounts(name) ON DELETE CASCADE,"
		" created TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)") == SQLITE_DONE
		&& Exec ("CREATE INDEX personas_account ON personas(account)") == SQLITE_DONE;
	if (ok) ImportRusers ();
	return ok;
}

// stats and track records, imported from stat.dat and s*.dat
static bool Schema2() {
	bool ok = Exec (
		"CREATE TABLE stats ("
		" persona TEXT NOT NULL COLLATE NOCASE REFERENCES personas(name) ON DELETE CASCADE,"
		" mode TEXT NOT NULL," // all, circuit, sprint, drag, drift
		" rating INTEGER NOT NULL, wins INTEGER NOT NULL, losses INTEGER NOT NULL,"
		" disconnects INTEGER NOT NULL, rep INTEGER NOT NULL,"
		" opp_rep INTEGER NOT NULL, opp_rating INTEGER NOT NULL," // opponents' averages
		" PRIMARY KEY (persona, mode))") == SQLITE_DONE
		&& Exec (
		"CREATE TABLE track_records ("
		" track INTEGER NOT NULL,"
		" persona TEXT NOT NULL COLLATE NOCASE REFERENCES personas(name) ON DELETE CASCADE,"
		" score INTEGER NOT NULL," // best lap in ms, or drift points on the drift tracks
		" car INTEGER NOT NULL, dir INTEGER NOT NULL,"
		" PRIMARY KEY (track, persona))") == SQLITE_DONE;
	if (ok) ImportStats ();
	// a persona without stats would crash CalcStat: give it what cper gives a new one
	return ok && Exec (
		"INSERT INTO stats SELECT p.name, m.mode, 9999, 0, 0, 0, 100, 101, 101 FROM personas p,"
		" (SELECT 'all' AS mode UNION ALL SELECT 'circuit' UNION ALL SELECT 'sprint'"
		"  UNION ALL SELECT 'drag' UNION ALL SELECT 'drift') m"
		" WHERE NOT EXISTS (SELECT 1 FROM stats s WHERE s.persona = p.name)") == SQLITE_DONE;
}

void DbClose() {
	sqlite3_close (db);
	db = NULL;
}

bool NameValid( const char * name, size_t min, size_t max ) {
	size_t len = strlen (name);
	if (len < min || len > max) return false;
	for (size_t i = 0; i < len; i++)
		if (!isalnum ((unsigned char)name[i])) return false;
	return true;
}

static int Hex( char c ) {
	if (c >= '0' && c <= '9') return c - '0';
	c = (char)tolower (c);
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

bool DecodePass( const char * field, char * out, size_t size ) {
	// shift per position, round the 96 printable characters; same for every PC
	// as long as the server sends the SKEY in the addr handler
	static const int key[17] = { 0, 9, 33, 69, 41, 38, 7, 58, 7, 27, 45, 14, 1, 84, 35, 29, 14 };
	char raw[64];
	size_t n = 0, len = strlen (field);

	if (len >= 2 && field[0] == '"' && field[len - 1] == '"') {
		field++;
		len -= 2;
	}
	for (size_t i = 0; i < len; i++) {
		char c = field[i];
		if (c == '%' && i + 2 < len && Hex (field[i + 1]) >= 0 && Hex (field[i + 2]) >= 0) {
			c = (char)(Hex (field[i + 1]) * 16 + Hex (field[i + 2]));
			i += 2;
		}
		if (n == sizeof (raw)) return false;
		raw[n++] = c;
	}
	if (n < 2 || raw[0] != '~') return false;

	for (size_t i = 0; i < 17 && i + 1 < n; i++) {
		int c = (unsigned char)raw[i + 1];
		if (c < 32 || c > 127) return false;
		c = (c - 32 - key[i] + 96 * 2) % 96 + 32;
		if (c == 127) { // end mark
			if (i == 0 || i >= size) return false;
			out[i] = 0;
			return true;
		}
		out[i] = (char)c;
	}
	return false;
}

static bool PassValid( const char * pass ) {
	size_t len = strlen (pass);
	if (len < 4 || len > 16) return false;
	for (size_t i = 0; i < len; i++)
		if (!isalnum ((unsigned char)pass[i])) return false;
	return true;
}

static bool Hash( const char * pass, char * out, size_t size ) {
	char salt[CRYPT_GENSALT_OUTPUT_SIZE];
	struct crypt_data cd;
	memset (&cd, 0, sizeof (cd));
	// yescrypt at libxcrypt's default cost: 11 ms on an i7-12650H, paid on the lobby thread
	if (crypt_gensalt_rn ("$y$", 0, NULL, 0, salt, sizeof (salt)) == NULL) return false;
	const char * h = crypt_rn (pass, salt, &cd, sizeof (cd));
	if (h == NULL || h[0] == '*' || strlen (h) >= size) return false;
	strcpy (out, h);
	return true;
}

static bool Verify( const char * pass, const char * hash ) {
	struct crypt_data cd;
	memset (&cd, 0, sizeof (cd));
	const char * h = crypt_rn (pass, hash, &cd, sizeof (cd));
	return h != NULL && h[0] != '*' && strcmp (h, hash) == 0;
}

bool LoadPersonas( const char * account, char personas[4][100] ) {
	memset (personas, 0, 4 * 100);
	sqlite3_stmt * st;
	int rc = Run (&st, "SELECT name FROM personas WHERE account = ? ORDER BY rowid LIMIT 4", account);
	for (int i = 0; rc == SQLITE_ROW; i++) {
		snprintf (personas[i], 100, "%s", (const char *)sqlite3_column_text (st, 0));
		rc = sqlite3_step (st);
	}
	sqlite3_finalize (st);
	return rc == SQLITE_DONE;
}

int AccountLogin( const char * given, const char * pass, const char * ip, char * name, char personas[4][100] ) {
	time_t now = time (NULL);
	if (Blocked (addressStrikes, ip, now)) return ACC_THROTTLED;

	sqlite3_stmt * st;
	int rc = Run (&st, "SELECT name, password_hash, banned, last_ip FROM accounts WHERE name = ?", given);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize (st);
		if (rc != SQLITE_DONE) return ACC_ERROR;
		Strike (addressStrikes, ip, ADDRESS_LIMIT, now);
		return ACC_NOUSER;
	}
	char hash[128] = { 0 };
	snprintf (name, 100, "%s", (const char *)sqlite3_column_text (st, 0));
	if (sqlite3_column_type (st, 1) != SQLITE_NULL)
		snprintf (hash, sizeof (hash), "%s", (const char *)sqlite3_column_text (st, 1));
	bool banned = sqlite3_column_int (st, 2) != 0;
	bool trusted = sqlite3_column_type (st, 3) != SQLITE_NULL && strcmp ((const char *)sqlite3_column_text (st, 3), ip) == 0;
	sqlite3_finalize (st);

	std::string account (name);
	for (size_t i = 0; i < account.size (); i++) account[i] = (char)tolower ((unsigned char)account[i]);
	if (!trusted && Blocked (accountStrikes, account, now)) return ACC_LOCKED;

	if (hash[0] == 0) {
		// imported from rusers.dat: the first login sets the password
		if (!PassValid (pass)) return ACC_BADPASS;
		if (!Hash (pass, hash, sizeof (hash))) return ACC_ERROR;
		if (Exec ("UPDATE accounts SET password_hash = ? WHERE name = ?", hash, name) != SQLITE_DONE) return ACC_ERROR;
	}
	else if (!Verify (pass, hash)) {
		Strike (addressStrikes, ip, ADDRESS_LIMIT, now);
		if (!trusted) Strike (accountStrikes, account, ACCOUNT_LIMIT, now);
		return ACC_WRONG;
	}
	if (banned) return ACC_BANNED;

	accountStrikes.erase (account);
	Exec ("UPDATE accounts SET last_login = CURRENT_TIMESTAMP, last_ip = ? WHERE name = ?", ip, name);
	return LoadPersonas (name, personas) ? ACC_OK : ACC_ERROR;
}

int AccountCreate( const char * name, const char * pass ) {
	char hash[128];
	if (!PassValid (pass)) return ACC_BADPASS;
	if (!Hash (pass, hash, sizeof (hash))) return ACC_ERROR;
	int rc = Exec ("INSERT INTO accounts (name, password_hash) VALUES (?, ?)", name, hash);
	if (rc == SQLITE_DONE) return ACC_OK;
	return rc == SQLITE_CONSTRAINT ? ACC_DUPL : ACC_ERROR;
}

// for the admin page: every account with its personas, by name
bool AccountList( std::vector<AccountInfo> & out ) {
	sqlite3_stmt * st = NULL;
	int rc = Run (&st, "SELECT a.name, a.created, a.last_login, (SELECT group_concat(p.name, ',') FROM personas p WHERE p.account = a.name)"
		" FROM accounts a ORDER BY a.name COLLATE NOCASE");
	for (; rc == SQLITE_ROW; rc = sqlite3_step (st)) {
		AccountInfo a;
		const char * v;
		a.name = (const char *)sqlite3_column_text (st, 0);
		if ((v = (const char *)sqlite3_column_text (st, 1)) != NULL) a.created = v;
		if ((v = (const char *)sqlite3_column_text (st, 2)) != NULL) a.lastLogin = v;
		if ((v = (const char *)sqlite3_column_text (st, 3)) != NULL) a.personas = v;
		out.push_back (a);
	}
	sqlite3_finalize (st);
	return rc == SQLITE_DONE;
}

// for the admin page: name receives the account as stored, personas what it had.
// Its personas, their stats and track records go with it (ON DELETE CASCADE).
int AccountDelete( const char * given, char * name, char personas[4][100] ) {
	sqlite3_stmt * st = NULL;
	int rc = Run (&st, "SELECT name FROM accounts WHERE name = ?", given);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize (st);
		return rc == SQLITE_DONE ? ACC_NOUSER : ACC_ERROR;
	}
	snprintf (name, 100, "%s", (const char *)sqlite3_column_text (st, 0));
	sqlite3_finalize (st);
	if (!LoadPersonas (name, personas)) return ACC_ERROR;
	if (Exec ("DELETE FROM accounts WHERE name = ?", name) != SQLITE_DONE) return ACC_ERROR;
	std::string account (name);
	for (size_t i = 0; i < account.size (); i++) account[i] = (char)tolower ((unsigned char)account[i]);
	accountStrikes.erase (account);
	return ACC_OK;
}

// for the admin page; also lifts a lock from wrong passwords
int AccountSetPassword( const char * name, const char * pass ) {
	char hash[128];
	if (!PassValid (pass)) return ACC_BADPASS;
	if (!Hash (pass, hash, sizeof (hash))) return ACC_ERROR;
	if (Exec ("UPDATE accounts SET password_hash = ? WHERE name = ?", hash, name) != SQLITE_DONE) return ACC_ERROR;
	if (sqlite3_changes (db) == 0) return ACC_NOUSER;
	std::string account (name);
	for (size_t i = 0; i < account.size (); i++) account[i] = (char)tolower ((unsigned char)account[i]);
	accountStrikes.erase (account);
	return ACC_OK;
}

int PersonaCreate( const char * account, const char * persona ) {
	sqlite3_stmt * st;
	Run (&st, "SELECT count(*) FROM personas WHERE account = ?", account);
	int count = sqlite3_column_int (st, 0);
	sqlite3_finalize (st);
	if (count >= 4) return ACC_MAXP;

	int rc = Exec ("INSERT INTO personas (name, account) VALUES (?, ?)", persona, account);
	if (rc == SQLITE_DONE) return ACC_OK;
	return rc == SQLITE_CONSTRAINT ? ACC_DUPL : ACC_ERROR;
}

bool PersonaDelete( const char * account, const char * persona ) {
	return Exec ("DELETE FROM personas WHERE name = ? AND account = ?", persona, account) == SQLITE_DONE;
}

// ---- stats and track records ----

static const char * const MODES[5] = { "all", "circuit", "sprint", "drag", "drift" };

// snap lists them in this order, from INDEX 6 on
const int TRACKS[TRACK_COUNT] = {
	1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008,	// circuit
	1102, 1103, 1104, 1105, 1106, 1107, 1108, 1109,	// sprint
	1201, 1202, 1206, 1207, 1210, 1214,			// drag
	1301, 1302, 1303, 1304, 1305, 1306, 1307, 1308	// drift: more points is better
};

static bool KnownTrack( int track ) {
	for (int i = 0; i < TRACK_COUNT; i++)
		if (TRACKS[i] == track) return true;
	return false;
}

// a statement with text, then int parameters; returns the step result, *out
// keeps the statement for reading rows
static int RunInts( sqlite3_stmt ** out, const char * sql, const char * text, const int * ints, int n ) {
	sqlite3_stmt * st;
	if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK) {
		DbError (sql);
		return SQLITE_ERROR;
	}
	int p = 1;
	if (text) sqlite3_bind_text (st, p++, text, -1, SQLITE_TRANSIENT);
	for (int i = 0; i < n; i++) sqlite3_bind_int (st, p++, ints[i]);
	int rc = sqlite3_step (st);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) DbError (sql);
	if (out) *out = st;
	else sqlite3_finalize (st);
	return rc;
}

void DbBegin() {
	Exec ("BEGIN");
}

void DbCommit() {
	Exec ("COMMIT");
}

bool StatSave( const char * persona, const int v[35] ) {
	for (int m = 0; m < 5; m++) {
		sqlite3_stmt * st;
		if (sqlite3_prepare_v2 (db, "INSERT OR REPLACE INTO stats (persona, mode, rating, wins, losses,"
			" disconnects, rep, opp_rep, opp_rating) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", -1, &st, NULL) != SQLITE_OK) {
			DbError ("stats");
			return false;
		}
		sqlite3_bind_text (st, 1, persona, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text (st, 2, MODES[m], -1, SQLITE_STATIC);
		for (int i = 0; i < 7; i++) sqlite3_bind_int (st, 3 + i, v[m * 7 + i]);
		int rc = sqlite3_step (st);
		sqlite3_finalize (st);
		if (rc != SQLITE_DONE) {
			if (rc != SQLITE_CONSTRAINT) DbError ("stats"); // a constraint: the persona is gone
			return false;
		}
	}
	return true;
}

bool StatLoadAll( void (*each)( const char * persona, const int v[35] ) ) {
	sqlite3_stmt * st;
	int rc = Run (&st, "SELECT persona, mode, rating, wins, losses, disconnects, rep, opp_rep, opp_rating"
		" FROM stats ORDER BY persona");
	char persona[100] = { 0 };
	int v[35] = { 0 };
	for (; rc == SQLITE_ROW; rc = sqlite3_step (st)) {
		const char * p = (const char *)sqlite3_column_text (st, 0);
		if (persona[0] != 0 && strcmp (persona, p) != 0) {
			each (persona, v);
			memset (v, 0, sizeof (v));
		}
		snprintf (persona, sizeof (persona), "%s", p);
		const char * mode = (const char *)sqlite3_column_text (st, 1);
		for (int m = 0; m < 5; m++)
			if (strcmp (mode, MODES[m]) == 0)
				for (int i = 0; i < 7; i++) v[m * 7 + i] = sqlite3_column_int (st, 2 + i);
	}
	if (persona[0] != 0) each (persona, v);
	sqlite3_finalize (st);
	return rc == SQLITE_DONE;
}

bool TrackRecordSave( int track, const char * persona, int score, int car, int dir ) {
	if (!KnownTrack (track) || score <= 0) return false;
	// replace the persona's record only by a better one
	const char * sql = track >= 1301
		? "INSERT INTO track_records (persona, track, score, car, dir) VALUES (?, ?, ?, ?, ?)"
		  " ON CONFLICT (track, persona) DO UPDATE SET score = excluded.score, car = excluded.car,"
		  " dir = excluded.dir WHERE excluded.score > track_records.score"
		: "INSERT INTO track_records (persona, track, score, car, dir) VALUES (?, ?, ?, ?, ?)"
		  " ON CONFLICT (track, persona) DO UPDATE SET score = excluded.score, car = excluded.car,"
		  " dir = excluded.dir WHERE excluded.score < track_records.score";
	int ints[4] = { track, score, car, dir };
	return RunInts (NULL, sql, persona, ints, 4) == SQLITE_DONE;
}

int TrackRecordsTop( int track, TrackRecord * out, int max ) {
	sqlite3_stmt * st;
	int ints[2] = { track, max };
	int rc = RunInts (&st, track >= 1301
		? "SELECT persona, score, car, dir FROM track_records WHERE track = ? ORDER BY score DESC, rowid LIMIT ?"
		: "SELECT persona, score, car, dir FROM track_records WHERE track = ? ORDER BY score ASC, rowid LIMIT ?",
		NULL, ints, 2);
	int n = 0;
	for (; rc == SQLITE_ROW && n < max; rc = sqlite3_step (st), n++) {
		snprintf (out[n].persona, sizeof (out[n].persona), "%s", (const char *)sqlite3_column_text (st, 0));
		out[n].score = sqlite3_column_int (st, 1);
		out[n].car = sqlite3_column_int (st, 2);
		out[n].dir = sqlite3_column_int (st, 3);
	}
	sqlite3_finalize (st);
	return n;
}

// stat.dat: PlayerStat records, Name[16] and 35 ints; s<track>.dat: Name[16],
// score, car, dir. Records of personas that no longer exist are dropped.
static void ImportStats() {
	char log[256], r[16 + 35 * 4];
	int personas = 0, records = 0;
	FILE * f = fopen ("stat.dat", "rb");
	if (f != NULL) {
		while (fread (r, 1, sizeof (r), f) == sizeof (r)) {
			char name[17] = { 0 };
			int v[35];
			memcpy (name, r, 16);
			memcpy (v, r + 16, sizeof (v));
			if (name[0] != 0 && StatSave (name, v)) personas++;
		}
		fclose (f);
	}
	for (int t = 0; t < TRACK_COUNT; t++) {
		char file[16];
		snprintf (file, sizeof (file), "s%d.dat", TRACKS[t]);
		f = fopen (file, "rb");
		if (f == NULL) continue;
		while (fread (r, 1, 28, f) == 28) {
			char name[17] = { 0 };
			int v[3];
			memcpy (name, r, 16);
			memcpy (v, r + 16, sizeof (v));
			if (name[0] != 0 && TrackRecordSave (TRACKS[t], name, v[0], v[1], v[2])) records++;
		}
		fclose (f);
	}
	snprintf (log, sizeof (log), "Imported stats of %d personas and %d track records\n", personas, records);
	Log (log);
}
