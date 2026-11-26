#ifndef ADMIN_H
#define ADMIN_H

#include <string>
#include <vector>

// The website's admin login: a password (its crypt hash in a file) plus a TOTP
// code (its base32 secret in a file). Without the TOTP file it runs in LAN mode:
// the password alone, accepted only from private addresses. Only the API thread calls these, except
// LogKeep, which every thread calls through Log.

enum { ADMIN_OK, ADMIN_WRONG, ADMIN_WAIT, ADMIN_FAR };

// reads the files; false (and the admin pages stay off) when one is missing or bad
bool AdminStart( const char * passwordFile, const char * totpFile );
bool AdminEnabled();
bool AdminLan();
bool PrivateAddress( const std::string & ip );
// ADMIN_WAIT: this address failed recently, try again in retry seconds;
// ADMIN_FAR: LAN mode and not a private address
int AdminLogin( const std::string & ip, const std::string & password, const std::string & code, std::string & token, int & retry );
bool AdminSession( const std::string & token );
void AdminLogout( const std::string & token );

// 10 letters and digits, within the game's password rules
std::string RandomPassword();

// the last 500 log lines, for the admin page
void LogKeep( const char * line );
std::vector<std::string> LogLines();

// RFC 6238 with SHA-1, 30 s steps, 6 digits; exposed for the self-check
int Totp( const unsigned char * key, size_t len, unsigned long long step );

#endif
