// JSON API for the website, plain HTTP on apiip:10990. Only a reverse proxy on
// the same host (or in the same Docker network) should reach it: it serves
// public data, and the proxy adds TLS.
//
//   GET /api/status                     server, its public address, uptime, rooms and who is in them
//   GET /api/races                      games with track, laps, players and cars
//   GET /api/leaderboard?mode=circuit   top 100 by REP like the game (all, circuit, sprint, drag, drift)
//   GET /api/records                    best 10 on every track

#include "win_nix.h"
#include "objects.h"
#include "accounts.h"
#include <string>

extern void Log(char* log);
extern bool running;
extern time_t curtime;
extern ConnectionsClass ClientConnections;
extern ServerClass Server;
extern SessionsClass Sessions;
extern std::vector<PlayerStat> PS;

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

static void Reply(SOCKET s, const char* status, const std::string& body) {
	char head[256];
	snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, (unsigned long)body.size());
	std::string all = std::string(head) + body;
	send(s, all.data(), all.size(), 0);
}

static void Handle(SOCKET s) {
	char buf[4096];
	size_t len = 0;
	int n;
	// the request line and headers; a body is never needed
	while (len < sizeof(buf) - 1 && (n = recv(s, buf + len, sizeof(buf) - 1 - len, 0)) > 0) {
		len += n;
		buf[len] = 0;
		if (strstr(buf, "\r\n\r\n") != NULL) break;
	}
	buf[len] = 0;
	char method[8], target[256];
	if (sscanf(buf, "%7s %255s HTTP/1.", method, target) != 2) {
		Reply(s, "400 Bad Request", "{\"error\":\"bad request\"}");
		return;
	}
	if (strcmp(method, "GET") != 0) {
		Reply(s, "405 Method Not Allowed", "{\"error\":\"only GET\"}");
		return;
	}
	char* query = strchr(target, '?');
	if (query != NULL) *query++ = 0;

	if (strcmp(target, "/api/status") == 0) Reply(s, "200 OK", Status());
	else if (strcmp(target, "/api/races") == 0) Reply(s, "200 OK", Races());
	else if (strcmp(target, "/api/records") == 0) Reply(s, "200 OK", Records());
	else if (strcmp(target, "/api/leaderboard") == 0) {
		int mode = 0;
		if (query != NULL && strncmp(query, "mode=", 5) == 0) {
			for (mode = 4; mode >= 0 && strcmp(query + 5, MODE_NAMES[mode]) != 0; mode--);
		}
		if (mode < 0) Reply(s, "400 Bad Request", "{\"error\":\"mode is all, circuit, sprint, drag or drift\"}");
		else Reply(s, "200 OK", Leaderboard(mode));
	}
	else Reply(s, "404 Not Found", "{\"error\":\"not found\"}");
}

// shortcut: one request at a time, each limited to 5 s; enough behind a local proxy, give each connection its own thread if pages get slow
static threadfunc ApiThread(void*) {
	timeval timeout = { 5, 0 };
	while (running) {
		SOCKET s = accept(ApiSocket, NULL, NULL);
		if (s == INVALID_SOCKET) {
			Sleep(100); // see AcceptThread: don't spin when accept keeps failing
			continue;
		}
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		Handle(s);
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
