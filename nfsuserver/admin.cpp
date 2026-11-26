// Admin login for the website: password hash + TOTP, sessions, a slowdown per
// address after wrong tries, and the log lines the admin page shows.

#include "win_nix.h"
#include "admin.h"
#include <map>
#include <deque>
#include <crypt.h>

extern void Log(char* log);

// --- SHA-1 and HMAC, only for TOTP (RFC 3174, RFC 2104) ---

static uint32_t Rol( uint32_t x, int c ) { return (x << c) | (x >> (32 - c)); }

static void Sha1Block( uint32_t h[5], const unsigned char * p ) {
	uint32_t w[80];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 80; i++) w[i] = Rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
	for (int i = 0; i < 80; i++) {
		uint32_t f, k;
		if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
		else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
		else { f = b ^ c ^ d; k = 0xCA62C1D6; }
		uint32_t t = Rol (a, 5) + f + e + k + w[i];
		e = d; d = c; c = Rol (b, 30); b = a; a = t;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void Sha1( const unsigned char * msg, size_t len, unsigned char out[20] ) {
	uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	size_t i = 0;
	for (; i + 64 <= len; i += 64) Sha1Block (h, msg + i);
	unsigned char tail[128];
	size_t rest = len - i, size = rest + 9 <= 64 ? 64 : 128;
	memcpy (tail, msg + i, rest);
	tail[rest] = 0x80;
	memset (tail + rest + 1, 0, size - rest - 1);
	unsigned long long bits = (unsigned long long)len * 8;
	for (int j = 0; j < 8; j++) tail[size - 1 - j] = (unsigned char)(bits >> (8 * j));
	Sha1Block (h, tail);
	if (size == 128) Sha1Block (h, tail + 64);
	for (int j = 0; j < 5; j++)
		for (int b = 0; b < 4; b++) out[4 * j + b] = (unsigned char)(h[j] >> (24 - 8 * b));
}

static void HmacSha1( const unsigned char * key, size_t klen, const unsigned char * msg, size_t mlen, unsigned char out[20] ) {
	unsigned char k[64] = { 0 }, inner[20];
	if (klen > 64) Sha1 (key, klen, k);
	else memcpy (k, key, klen);
	std::vector<unsigned char> in (64 + mlen);
	for (int i = 0; i < 64; i++) in[i] = k[i] ^ 0x36;
	if (mlen) memcpy (&in[64], msg, mlen);
	Sha1 (&in[0], in.size (), inner);
	unsigned char o[84];
	for (int i = 0; i < 64; i++) o[i] = k[i] ^ 0x5c;
	memcpy (o + 64, inner, 20);
	Sha1 (o, sizeof (o), out);
}

int Totp( const unsigned char * key, size_t len, unsigned long long step ) {
	unsigned char msg[8], h[20];
	for (int i = 0; i < 8; i++) msg[7 - i] = (unsigned char)(step >> (8 * i));
	HmacSha1 (key, len, msg, 8, h);
	int o = h[19] & 0x0f;
	uint32_t bin = (uint32_t)(h[o] & 0x7f) << 24 | (uint32_t)h[o + 1] << 16 | (uint32_t)h[o + 2] << 8 | h[o + 3];
	return (int)(bin % 1000000);
}

// --- secrets ---

static std::string passwordHash;
static std::vector<unsigned char> totpKey;
static unsigned long long lastStep = 0; // a code is good once

static bool ReadFirstLine( const char * path, std::string & out ) {
	FILE * f = fopen (path, "r");
	if (f == NULL) return false;
	char line[512] = "";
	bool ok = fgets (line, sizeof (line), f) != NULL;
	fclose (f);
	out = line;
	while (!out.empty () && isspace ((unsigned char)out[out.size () - 1])) out.erase (out.size () - 1);
	return ok && !out.empty ();
}

// RFC 4648 base32, as authenticator apps show it: case and spaces don't matter
static bool Base32( const std::string & s, std::vector<unsigned char> & out ) {
	static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	unsigned int buffer = 0;
	int bits = 0;
	out.clear ();
	for (size_t i = 0; i < s.size (); i++) {
		if (s[i] == ' ' || s[i] == '=' || s[i] == '-') continue;
		const char * at = strchr (abc, toupper ((unsigned char)s[i]));
		if (at == NULL || *at == 0) return false;
		buffer = (buffer << 5) | (unsigned int)(at - abc);
		bits += 5;
		if (bits >= 8) {
			out.push_back ((unsigned char)(buffer >> (bits - 8)));
			bits -= 8;
		}
	}
	return out.size () >= 10; // 80 bits at least
}

bool AdminStart( const char * passwordFile, const char * totpFile ) {
	char log[512];
	// RFC 6238, appendix B: the ASCII key 12345678901234567890 gives 94287082 at 59 s
	if (Totp ((const unsigned char *)"12345678901234567890", 20, 1) != 287082) {
		Log ((char *)"[error] TOTP self-check failed, admin login off.\n");
		return false;
	}
	if (passwordFile[0] == 0 && totpFile[0] == 0) {
		Log ((char *)"Admin login off: adminpasswordfile and admintotpfile are not set.\n");
		return false;
	}
	std::string secret;
	if (!ReadFirstLine (passwordFile, passwordHash) || passwordHash[0] != '$') {
		snprintf (log, sizeof (log), "[error] Admin login off: %s holds no password hash (make one with mkpasswd -m yescrypt).\n", passwordFile);
		Log (log);
		passwordHash.clear ();
		return false;
	}
	if (!ReadFirstLine (totpFile, secret) || !Base32 (secret, totpKey)) {
		snprintf (log, sizeof (log), "[error] Admin login off: %s holds no base32 TOTP secret.\n", totpFile);
		Log (log);
		passwordHash.clear ();
		return false;
	}
	Log ((char *)"Admin login on.\n");
	return true;
}

bool AdminEnabled() { return !passwordHash.empty (); }

// --- randomness ---

static bool Random( unsigned char * out, size_t n ) {
	FILE * f = fopen ("/dev/urandom", "rb");
	if (f == NULL) return false;
	bool ok = fread (out, 1, n, f) == n;
	fclose (f);
	return ok;
}

std::string RandomPassword() {
	static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	std::string out;
	unsigned char b[64];
	while (out.size () < 10 && Random (b, sizeof (b)))
		for (size_t i = 0; i < sizeof (b) && out.size () < 10; i++)
			if (b[i] < 248) out += abc[b[i] % 62]; // 248 = 4 x 62: no letter more likely than another
	return out;
}

// --- sessions and wrong tries ---

static const int SESSION_SECONDS = 12 * 3600, MAX_WAIT = 15 * 60;
static std::map<std::string, time_t> sessions;
struct Tries { int count; time_t until; };
static std::map<std::string, Tries> tries;

static bool CheckCode( const std::string & code, time_t now ) {
	if (code.size () != 6 || code.find_first_not_of ("0123456789") != std::string::npos) return false;
	int want = atoi (code.c_str ());
	unsigned long long step = (unsigned long long)now / 30;
	for (int d = -1; d <= 1; d++) // the phone's clock may be 30 s off
		if (step + d > lastStep && Totp (&totpKey[0], totpKey.size (), step + d) == want) {
			lastStep = step + d;
			return true;
		}
	return false;
}

int AdminLogin( const std::string & ip, const std::string & password, const std::string & code, std::string & token, int & retry ) {
	time_t now = time (NULL);
	std::map<std::string, Tries>::iterator it = tries.find (ip);
	if (it != tries.end () && it->second.until > now) {
		retry = (int)(it->second.until - now);
		return ADMIN_WAIT;
	}
	struct crypt_data cd;
	memset (&cd, 0, sizeof (cd));
	const char * h = crypt_rn (password.c_str (), passwordHash.c_str (), &cd, sizeof (cd));
	bool passOk = h != NULL && h[0] != '*' && passwordHash == h;
	if (!passOk || !CheckCode (code, now)) { // the code is checked only with the right password: no hint which was wrong
		if (tries.size () > 10000) // many addresses must not fill the memory
			for (it = tries.begin (); it != tries.end ();)
				if (it->second.until <= now - MAX_WAIT) tries.erase (it++); else ++it;
		Tries & t = tries[ip];
		t.count = t.until > now - MAX_WAIT ? t.count + 1 : 1;
		int wait = t.count >= 11 ? MAX_WAIT : 1 << (t.count - 1); // 1, 2, 4 ... seconds, at most 15 minutes
		t.until = now + (wait < MAX_WAIT ? wait : MAX_WAIT);
		return ADMIN_WRONG;
	}
	tries.erase (ip);
	unsigned char r[32];
	if (!Random (r, sizeof (r))) return ADMIN_WRONG;
	char hex[65];
	for (int i = 0; i < 32; i++) sprintf (hex + 2 * i, "%02x", r[i]);
	token = hex;
	for (std::map<std::string, time_t>::iterator s = sessions.begin (); s != sessions.end ();)
		if (s->second <= now) sessions.erase (s++); else ++s;
	sessions[token] = now + SESSION_SECONDS;
	return ADMIN_OK;
}

bool AdminSession( const std::string & token ) {
	std::map<std::string, time_t>::iterator s = sessions.find (token);
	return !token.empty () && s != sessions.end () && s->second > time (NULL);
}

void AdminLogout( const std::string & token ) { sessions.erase (token); }

// --- log lines ---

static pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;
static std::deque<std::string> logLines;

void LogKeep( const char * line ) {
	pthread_mutex_lock (&logLock);
	logLines.push_back (line);
	if (logLines.size () > 500) logLines.pop_front ();
	pthread_mutex_unlock (&logLock);
}

std::vector<std::string> LogLines() {
	pthread_mutex_lock (&logLock);
	std::vector<std::string> out (logLines.begin (), logLines.end ());
	pthread_mutex_unlock (&logLock);
	return out;
}
