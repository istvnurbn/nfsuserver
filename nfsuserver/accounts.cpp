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
	if (version > 0) return true;

	// first start: schema and the one-time import in one transaction
	Exec ("BEGIN");
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
	if (ok && Exec ("PRAGMA user_version = 1") == SQLITE_DONE && Exec ("COMMIT") == SQLITE_DONE) return true;
	Exec ("ROLLBACK");
	return false;
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
