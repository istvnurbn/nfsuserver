// JSON API for the website, plain HTTP on apiip:10990. Only a reverse proxy on
// the same host (or in the same Docker network) should reach it: it serves
// public data, and the proxy adds TLS.
//
//   GET /api/status                     server, its public address, uptime, rooms and who is in them
//   GET /api/races                      games with track, laps, players and cars
//   GET /api/leaderboard?mode=circuit   top 100 by REP like the game (all, circuit, sprint, drag, drift)
//   GET /api/records                    best 10 on every track
//
// Admin (only with adminpasswordfile and admintotpfile set; POSTs take JSON):
//   POST /api/admin/login     {"password", "code"} sets the session cookie; no code in LAN mode
//   POST /api/admin/logout
//   GET  /api/admin/session   200 while logged in, else 401; "totp" false in LAN mode
//   GET  /api/admin/accounts  every account with its personas, created and last login
//   POST /api/admin/accounts  {"account", "password"} adds a player; an empty password makes one up and returns it
//   POST /api/admin/password  {"account", "password"}: an empty password makes one up and returns it
//   POST /api/admin/delete    {"account"} removes it with its personas, stats and track records
//   GET  /api/admin/log       the last 500 log lines
//   GET  /api/admin/config    nfsu.conf

#include "win_nix.h"
#include "objects.h"
#include "accounts.h"
#include "admin.h"
#include <string>
#include <map>

extern void Log(char* log);
extern bool running;
extern time_t curtime;
extern ConnectionsClass ClientConnections;
extern ServerClass Server;
extern SessionsClass Sessions;
extern std::vector<PlayerStat> PS;

extern char ApiProxyIP[100];

static SOCKET ApiSocket = INVALID_SOCKET;
static const char* ApiVersion = "";

static const char* const MODE_NAMES[5] = { "all", "circuit", "sprint", "drag", "drift" };
// room letters follow the rooms_* keys of nfsu.conf: A-D ranked, E-H unranked
static const char* const ROOM_MODES[4] = { "circuit", "sprint", "drift", "drag" };

static void Str(std::string& out, const char* s) {
	char u[8];
	out += '"';
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
		else if (c < 0x20 || c >= 0x7f) { sprintf(u, "\\u%04x", c); out += u; } // bytes above ASCII read as Latin-1
		else out += (char)c;
	}
	out += '"';
}

static void Int(std::string& out, long v) {
	char b[24];
	sprintf(b, "%ld", v);
	out += b;
}

// "A.Global": the letter is the type, A-D ranked and E-H unranked
static void RoomFields(std::string& out, const char* name) {
	char type[2] = { name[0], 0 };
	int t = name[0] - 'A';
	out += "\"type\":"; Str(out, type);
	out += ",\"name\":"; Str(out, name[0] && name[1] == '.' ? name + 2 : name);
	if (t >= 0 && t < 8) {
		out += ",\"mode\":"; Str(out, ROOM_MODES[t % 4]);
		out += t < 4 ? ",\"ranked\":true" : ",\"ranked\":false";
	}
}

// PARAMS as the game sends it: URL-encoded lines TRACK=1001, DIR=0, LAPS=3, MYIP=<host address>.
// Returns the number after key, or -1. MYIP is never passed on.
static long Param(const char* params, const char* key) {
	std::string s;
	for (const char* p = params; *p; p++) {
		if (p[0] == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
			char h[3] = { p[1], p[2], 0 };
			s += (char)strtol(h, NULL, 16);
			p += 2;
		} else s += *p;
	}
	std::string k = std::string("\n") + key + "=";
	std::string::size_type at = ("\n" + s).find(k);
	if (at == std::string::npos) return -1;
	const char* v = s.c_str() + at + k.size() - 1;
	return isdigit((unsigned char)*v) ? strtol(v, NULL, 10) : -1;
}

// the car is the third character of the AUXI text, read as base64: A = 0 (VW Golf GTI) ... T = 19 (Skyline)
static long CarId(const char* car) {
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (strlen(car) < 3) return -1;
	const char* at = strchr(b64, car[2]);
	return at ? at - b64 : -1;
}

static void Num(std::string& out, long v) {
	if (v < 0) out += "null"; else Int(out, v);
}

static std::string Status() {
	std::string out = "{\"name\":";
	ClientConnections.mut.Lock();
	Str(out, Server.Name);
	out += ",\"address\":"; Str(out, Server.ServerExternalIP); // empty when not set
	out += ",\"version\":"; Str(out, ApiVersion);
	out += ",\"platform\":"; Str(out, SERVER_PLATFORM);
	out += ",\"uptime\":"; Int(out, (long)difftime(curtime, Server.Startup));
	out += ",\"players\":"; Int(out, ClientConnections.Count);
	out += ",\"racing\":"; Int(out, Sessions.Count);
	out += ",\"rooms\":[";
	for (RoomClass* r = Server.Rooms.First; r != NULL; r = r->Next) {
		out += r == Server.Rooms.First ? "{" : ",{";
		RoomFields(out, r->Name);
		out += ",\"players\":[";
		for (RUserClass* u = r->Users.First; u != NULL; u = u->Next) {
			if (u != r->Users.First) out += ",";
			Str(out, u->User->Personas[u->User->SelectedPerson]);
		}
		out += "]}";
	}
	ClientConnections.mut.Unlock();
	return out + "]}";
}

static std::string Races() {
	std::string out = "{\"races\":[";
	bool first = true;
	ClientConnections.mut.Lock();
	for (RoomClass* r = Server.Rooms.First; r != NULL; r = r->Next) {
		for (GameClass* g = r->Games.First; g != NULL; g = g->Next) {
			out += first ? "{\"room\":{" : ",{\"room\":{";
			first = false;
			RoomFields(out, r->Name);
			out += "},\"name\":"; Str(out, g->Name);
			out += g->working ? ",\"started\":true" : ",\"started\":false";
			out += ",\"track\":"; Num(out, Param(g->params, "TRACK"));
			out += ",\"laps\":"; Num(out, Param(g->params, "LAPS"));
			out += ",\"dir\":"; Num(out, Param(g->params, "DIR"));
			out += ",\"max\":"; Int(out, g->max);
			out += ",\"players\":[";
			for (RUserClass* u = g->Users.First; u != NULL; u = u->Next) {
				out += u == g->Users.First ? "{\"persona\":" : ",{\"persona\":";
				Str(out, u->User->Personas[u->User->SelectedPerson]);
				out += ",\"car\":"; Num(out, CarId(u->User->car));
				out += "}";
			}
			out += "]}";
		}
	}
	ClientConnections.mut.Unlock();
	return out + "]}";
}

struct ByRepIn {
	int mode;
	ByRepIn(int m) : mode(m) {}
	bool operator()(const PlayerStat& a, const PlayerStat& b) const {
		return (&a.Rating_All)[7 * mode + 4] > (&b.Rating_All)[7 * mode + 4];
	}
};

static std::string Leaderboard(int mode) {
	ClientConnections.mut.Lock();
	std::vector<PlayerStat> ps(PS);
	ClientConnections.mut.Unlock();
	std::stable_sort(ps.begin(), ps.end(), ByRepIn(mode)); // the order the game's rank list shows

	std::string out = "{\"mode\":";
	Str(out, MODE_NAMES[mode]);
	out += ",\"players\":[";
	for (size_t i = 0; i < ps.size() && i < 100; i++) {
		const int* v = &ps[i].Rating_All + 7 * mode; // rating, wins, losses, disconnects, REP
		out += i ? ",{\"position\":" : "{\"position\":";
		Int(out, (long)i + 1);
		out += ",\"persona\":"; Str(out, ps[i].Name);
		out += ",\"rating\":"; Int(out, v[0]);
		out += ",\"wins\":"; Int(out, v[1]);
		out += ",\"losses\":"; Int(out, v[2]);
		out += ",\"disconnects\":"; Int(out, v[3]);
		out += ",\"rep\":"; Int(out, v[4]);
		out += "}";
	}
	return out + "]}";
}

static std::string Records() {
	std::string out = "{\"tracks\":[";
	TrackRecord rec[10];
	for (int t = 0; t < TRACK_COUNT; t++) {
		ClientConnections.mut.Lock(); // the lobby uses the database under this lock too
		int n = TrackRecordsTop(TRACKS[t], rec, 10);
		ClientConnections.mut.Unlock();
		out += t ? ",{\"track\":" : "{\"track\":";
		Int(out, TRACKS[t]);
		out += ",\"records\":[";
		for (int i = 0; i < n; i++) {
			out += i ? ",{\"persona\":" : "{\"persona\":";
			Str(out, rec[i].persona);
			out += ",\"score\":"; Int(out, rec[i].score);
			out += ",\"car\":"; Int(out, rec[i].car);
			out += ",\"dir\":"; Int(out, rec[i].dir);
			out += "}";
		}
		out += "]}";
	}
	return out + "]}";
}

struct Request {
	std::string method, path, query, headers, body, ip;
	// a header's value, or "" (headers is the block after the request line, lines start with \r\n)
	std::string Header(const char* name) const {
		std::string lower(headers), key = std::string("\r\n") + name + ":";
		for (size_t i = 0; i < lower.size(); i++) lower[i] = (char)tolower((unsigned char)lower[i]);
		for (size_t i = 0; i < key.size(); i++) key[i] = (char)tolower((unsigned char)key[i]);
		std::string::size_type at = lower.find(key);
		if (at == std::string::npos) return "";
		std::string::size_type from = headers.find_first_not_of(" \t", at + key.size());
		std::string::size_type to = headers.find("\r\n", at + key.size());
		return from == std::string::npos || from >= to ? "" : headers.substr(from, to - from);
	}
	std::string Cookie(const char* name) const {
		std::string c = "; " + Header("Cookie") + ";", key = std::string("; ") + name + "=";
		std::string::size_type at = c.find(key);
		if (at == std::string::npos) return "";
		at += key.size();
		return c.substr(at, c.find(';', at) - at);
	}
};

// A flat JSON object of strings: {"a": "x", "b": "y"}. Anything else fails.
static bool JsonStrings(const std::string& s, std::map<std::string, std::string>& out) {
	size_t i = 0;
	struct P {
		static void Space(const std::string& s, size_t& i) { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
		static bool String(const std::string& s, size_t& i, std::string& v) {
			if (i >= s.size() || s[i] != '"') return false;
			v.clear();
			for (i++; i < s.size() && s[i] != '"'; i++) {
				unsigned char c = (unsigned char)s[i];
				if (c < 0x20) return false;
				if (c != '\\') { v += (char)c; continue; }
				if (++i >= s.size()) return false;
				switch (s[i]) {
				case '"': case '\\': case '/': v += s[i]; break;
				case 'n': v += '\n'; break;
				case 't': v += '\t'; break;
				case 'u': {
					if (i + 4 >= s.size()) return false;
					long u = strtol(s.substr(i + 1, 4).c_str(), NULL, 16);
					v += u > 0 && u < 0x80 ? (char)u : '?'; // names and passwords are ASCII
					i += 4;
					break;
				}
				default: return false;
				}
			}
			if (i >= s.size()) return false;
			i++;
			return true;
		}
	};
	P::Space(s, i);
	if (i >= s.size() || s[i++] != '{') return false;
	P::Space(s, i);
	if (i < s.size() && s[i] == '}') return true;
	for (;;) {
		std::string k, v;
		P::Space(s, i);
		if (!P::String(s, i, k)) return false;
		P::Space(s, i);
		if (i >= s.size() || s[i++] != ':') return false;
		P::Space(s, i);
		if (!P::String(s, i, v)) return false;
		out[k] = v;
		P::Space(s, i);
		if (i < s.size() && s[i] == ',') { i++; continue; }
		return i < s.size() && s[i] == '}';
	}
}

static void Reply(SOCKET s, const char* status, const std::string& body, const std::string& extra = "") {
	char head[256];
	snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\n", status, (unsigned long)body.size());
	std::string all = std::string(head) + extra + "\r\n" + body;
	send(s, all.data(), all.size(), 0);
}

static const char* const SESSION_COOKIE = "nfsu_admin";

static void Admin(SOCKET s, const Request& r) {
	char log[256];
	std::map<std::string, std::string> in;
	std::string action = r.path.substr(strlen("/api/admin/"));
	bool post = r.method == "POST";
	if (!AdminEnabled()) {
		Reply(s, "404 Not Found", "{\"error\":\"not found\"}");
		return;
	}
	if (post && (r.Header("Content-Type").compare(0, 16, "application/json") != 0 || !JsonStrings(r.body, in))) {
		// a form on another site can't send JSON without asking first
		Reply(s, "415 Unsupported Media Type", "{\"error\":\"send a JSON object of strings\"}");
		return;
	}
	if (action == "login" && post) {
		std::string token;
		int retry = 0;
		int rc = AdminLogin(r.ip, in["password"], in["code"], token, retry);
		if (rc == ADMIN_WAIT) {
			char body[64], extra[48];
			snprintf(body, sizeof(body), "{\"error\":\"wait\",\"retry\":%d}", retry);
			snprintf(extra, sizeof(extra), "Retry-After: %d\r\n", retry);
			Reply(s, "429 Too Many Requests", body, extra);
		} else if (rc == ADMIN_WRONG) {
			snprintf(log, sizeof(log), "Admin login failed from %s.\n", r.ip.c_str());
			Log(log);
			Reply(s, "401 Unauthorized", "{\"error\":\"wrong password or code\"}");
		} else {
			snprintf(log, sizeof(log), "Admin logged in from %s.\n", r.ip.c_str());
			Log(log);
			Reply(s, "200 OK", "{\"admin\":true}", std::string("Set-Cookie: ") + SESSION_COOKIE + "=" + token
				+ "; Path=/api/admin; Max-Age=43200; HttpOnly; " + (AdminLan() ? "" : "Secure; ") + "SameSite=Strict\r\n");
		}
		return;
	}
	std::string token = r.Cookie(SESSION_COOKIE);
	if (!AdminSession(token)) {
		Reply(s, "401 Unauthorized", "{\"error\":\"log in first\"" + totp);
		return;
	}
	if (action == "session" && !post) Reply(s, "200 OK", "{\"admin\":true" + totp);
	else if (action == "logout" && post) {
		AdminLogout(token);
		Reply(s, "200 OK", "{\"admin\":false}", std::string("Set-Cookie: ") + SESSION_COOKIE
			+ "=; Path=/api/admin; Max-Age=0; HttpOnly; Secure; SameSite=Strict\r\n");
	}
	else if (action == "password" && post) {
		std::string account = in["account"], password = in["password"];
		bool made = password.empty();
		if (made) password = RandomPassword();
		} else if (rc == ADMIN_FAR) {
			snprintf(log, sizeof(log), "Admin login refused from %s: LAN mode takes private addresses only.\n", r.ip.c_str());
			Log(log);
			Reply(s, "403 Forbidden", "{\"error\":\"the admin login works only from the local network\"}");
		ClientConnections.mut.Lock(); // the lobby uses the database under this lock too
		int rc = AccountSetPassword(account.c_str(), password.c_str());
		ClientConnections.mut.Unlock();
		if (rc == ACC_NOUSER) Reply(s, "404 Not Found", "{\"error\":\"no such account\"}");
		else if (rc == ACC_BADPASS) Reply(s, "400 Bad Request", "{\"error\":\"passwords are 4-16 letters and digits\"}");
		else if (rc != ACC_OK) Reply(s, "500 Internal Server Error", "{\"error\":\"could not save the password\"}");
		else {
			// a LAN often has no HTTPS, and a browser keeps no Secure cookie from plain HTTP
			snprintf(log, sizeof(log), "Admin set a new password for account %.100s.\n", account.c_str());
			Log(log);
			std::string out = "{\"account\":";
			Str(out, account.c_str());
			if (made) { out += ",\"password\":"; Str(out, password.c_str()); }
			Reply(s, "200 OK", out + "}");
	std::string totp = AdminLan() ? ",\"totp\":false}" : ",\"totp\":true}";
		}
	}
	else if (action == "accounts" && !post) {
		std::vector<AccountInfo> list;
		ClientConnections.mut.Lock();
		bool ok = AccountList(list);
		ClientConnections.mut.Unlock();
		if (!ok) {
			Reply(s, "500 Internal Server Error", "{\"error\":\"could not read the accounts\"}");
			return;
		}
		std::string out = "{\"accounts\":[";
		for (size_t i = 0; i < list.size(); i++) {
			out += i ? ",{\"account\":" : "{\"account\":";
			Str(out, list[i].name.c_str());
			out += ",\"personas\":[";
			std::string::size_type from = 0, to;
			while (!list[i].personas.empty() && from <= list[i].personas.size()) {
				to = list[i].personas.find(',', from);
				if (to == std::string::npos) to = list[i].personas.size();
				if (from) out += ",";
				Str(out, list[i].personas.substr(from, to - from).c_str());
				from = to + 1;
			}
			out += "],\"created\":";
			Str(out, list[i].created.c_str());
			out += ",\"last_login\":";
			if (list[i].lastLogin.empty()) out += "null"; else Str(out, list[i].lastLogin.c_str());
			out += "}";
		}
		Reply(s, "200 OK", out + "]}");
	}
	else if (action == "accounts" && post) {
		std::string account = in["account"], password = in["password"];
		bool made = password.empty();
		if (made) password = RandomPassword();
		if (!NameValid(account.c_str(), 4, 16)) {
			Reply(s, "400 Bad Request", "{\"error\":\"account names are 4-16 letters and digits\"}");
			return;
		}
		ClientConnections.mut.Lock();
		int rc = AccountCreate(account.c_str(), password.c_str());
		ClientConnections.mut.Unlock();
		if (rc == ACC_DUPL) Reply(s, "409 Conflict", "{\"error\":\"that account exists already\"}");
		else if (rc == ACC_BADPASS) Reply(s, "400 Bad Request", "{\"error\":\"passwords are 4-16 letters and digits\"}");
		else if (rc != ACC_OK) Reply(s, "500 Internal Server Error", "{\"error\":\"could not add the account\"}");
		else {
			snprintf(log, sizeof(log), "Admin added account %.100s.\n", account.c_str());
			Log(log);
			std::string out = "{\"account\":";
			Str(out, account.c_str());
			if (made) { out += ",\"password\":"; Str(out, password.c_str()); }
			Reply(s, "200 OK", out + "}");
		}
	}
	else if (action == "delete" && post) {
		char name[100], personas[4][100];
		std::string gone;
		ClientConnections.mut.Lock();
		int rc = AccountDelete(in["account"].c_str(), name, personas);
		if (rc == ACC_OK) {
			for (int i = 0; i < 4 && personas[i][0]; i++) {
				std::vector<PlayerStat>::iterator it = std::find(PS.begin(), PS.end(), personas[i]);
				if (it != PS.end()) PS.erase(it); // off the rank lists now, not after a restart
				gone += (i ? ", " : "") + std::string(personas[i]);
			}
			// a new account of that name must not find this player in memory, with these personas
			for (UserClass* u = Server.Users.First, *next; u != NULL; u = next) {
				next = u->Next;
				if (strcmp(u->Username, name) != 0) continue;
				if (u->Connection == NULL) { // offline: drop it now, as Maintenance does after 45 minutes
					Server.Users.RemoveUser(u);
					free(u);
				} else { // online: disconnect; Maintenance drops it once the connection is closed
					u->Connection->Abort = true;
					u->Deleted = true;
				}
			}
		}
		ClientConnections.mut.Unlock();
		if (rc == ACC_NOUSER) Reply(s, "404 Not Found", "{\"error\":\"no such account\"}");
		else if (rc != ACC_OK) Reply(s, "500 Internal Server Error", "{\"error\":\"could not delete the account\"}");
		else {
			snprintf(log, sizeof(log), "Admin deleted account %s (personas: %s).\n", name, gone.empty() ? "none" : gone.c_str());
			Log(log);
			std::string out = "{\"account\":";
			Str(out, name);
			Reply(s, "200 OK", out + "}");
		}
	}
	else if (action == "log" && !post) {
		std::vector<std::string> lines = LogLines();
		std::string out = "{\"lines\":[";
		for (size_t i = 0; i < lines.size(); i++) {
			if (i) out += ",";
			Str(out, lines[i].c_str());
		}
		Reply(s, "200 OK", out + "]}");
	}
	else if (action == "config" && !post) {
		std::string text;
		FILE* f = fopen("nfsu.conf", "r");
		char buf[1024];
		size_t n;
		while (f != NULL && (n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 65536) text.append(buf, n);
		if (f != NULL) fclose(f);
		std::string out = "{\"text\":";
		Str(out, text.c_str());
		Reply(s, "200 OK", out + "}");
	}
	else Reply(s, "404 Not Found", "{\"error\":\"not found\"}");
}

// reads one request: the head up to 8 KiB, a body up to 4 KiB
static bool Read(SOCKET s, Request& r) {
	std::string data;
	char buf[2048];
	int n;
	std::string::size_type end;
	while ((end = data.find("\r\n\r\n")) == std::string::npos) {
		if (data.size() > 8192 || (n = recv(s, buf, sizeof(buf), 0)) <= 0) return false;
		data.append(buf, n);
	}
	std::string::size_type line = data.find("\r\n");
	std::string first = data.substr(0, line);
	r.headers = data.substr(line, end + 2 - line);
	r.body = data.substr(end + 4);
	std::string::size_type a = first.find(' '), b = first.find(' ', a + 1);
	if (a == std::string::npos || b == std::string::npos || first.compare(b + 1, 7, "HTTP/1.") != 0) return false;
	r.method = first.substr(0, a);
	r.path = first.substr(a + 1, b - a - 1);
	std::string::size_type q = r.path.find('?');
	if (q != std::string::npos) {
		r.query = r.path.substr(q + 1);
		r.path.erase(q);
	}
	long length = atol(r.Header("Content-Length").c_str());
	if (length < 0 || length > 4096) return false;
	while ((long)r.body.size() < length) {
		if ((n = recv(s, buf, sizeof(buf), 0)) <= 0) return false;
		r.body.append(buf, n);
	}
	r.body.erase(length);
	return true;
}

static void Handle(SOCKET s, const char* peer) {
	Request r;
	if (!Read(s, r)) {
		Reply(s, "400 Bad Request", "{\"error\":\"bad request\"}");
		return;
	}
	// the proxy says who the visitor is; nobody else may
	std::string real = r.Header("X-Real-IP");
	r.ip = strcmp(peer, ApiProxyIP) == 0 && !real.empty() ? real.substr(0, 45) : peer;

	if (r.path.compare(0, 11, "/api/admin/") == 0) {
		Admin(s, r);
		return;
	}
	if (r.method != "GET") {
		Reply(s, "405 Method Not Allowed", "{\"error\":\"only GET\"}");
		return;
	}
	if (r.path == "/api/status") Reply(s, "200 OK", Status());
	else if (r.path == "/api/races") Reply(s, "200 OK", Races());
	else if (r.path == "/api/records") Reply(s, "200 OK", Records());
	else if (r.path == "/api/leaderboard") {
		int mode = 0;
		if (r.query.compare(0, 5, "mode=") == 0)
			for (mode = 4; mode >= 0 && r.query.substr(5) != MODE_NAMES[mode]; mode--);
		if (mode < 0) Reply(s, "400 Bad Request", "{\"error\":\"mode is all, circuit, sprint, drag or drift\"}");
		else Reply(s, "200 OK", Leaderboard(mode));
	}
	else Reply(s, "404 Not Found", "{\"error\":\"not found\"}");
}

// shortcut: one request at a time, each limited to 5 s; enough behind a local proxy, give each connection its own thread if pages get slow
static threadfunc ApiThread(void*) {
	timeval timeout = { 5, 0 };
	while (running) {
		SOCKADDR_IN from;
		socklen_t len = sizeof(from);
		SOCKET s = accept(ApiSocket, (SOCKADDR*)&from, &len);
		if (s == INVALID_SOCKET) {
			Sleep(100); // see AcceptThread: don't spin when accept keeps failing
			continue;
		}
		char peer[INET_ADDRSTRLEN] = "";
		inet_ntop(AF_INET, &from.sin_addr, peer, sizeof(peer));
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		Handle(s, peer);
		closesocket(s);
	}
	RETURNFROMTHREAD;
}

void ApiStart(const char* ip, const char* version) {
	char log[256];
	SOCKADDR_IN sin;
	int yes = 1;
	ApiVersion = version;
	if (ip[0] == '\0') return;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(10990);
	ApiSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (ApiSocket == INVALID_SOCKET || inet_pton(AF_INET, ip, &sin.sin_addr) != 1
		|| setsockopt(ApiSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0
		|| bind(ApiSocket, (SOCKADDR*)&sin, sizeof(sin)) != 0 || listen(ApiSocket, 20) != 0) {
		// the game works without the website
		snprintf(log, sizeof(log), "[error] Could not start the JSON API on %s:10990, running without it.\n", ip);
		Log(log);
		if (ApiSocket != INVALID_SOCKET) closesocket(ApiSocket);
		return;
	}
	snprintf(log, sizeof(log), "Listening for JSON API requests on %s:10990 TCP\n", ip);
	Log(log);
	_beginthread(ApiThread, 0, NULL);
}
