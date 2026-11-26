#ifndef ADMIN_H
#define ADMIN_H

#include <string>
#include <vector>

// The website's admin login: a password (its crypt hash in a file) plus a TOTP
// code (its base32 secret in a file). Only the API thread calls these, except
// LogKeep, which every thread calls through Log.

enum { ADMIN_OK, ADMIN_WRONG, ADMIN_WAIT };

// reads both files; false (and the admin pages stay off) when either is missing or bad
bool AdminStart( const char * passwordFile, const char * totpFile );
bool AdminEnabled();
// ADMIN_WAIT: this address failed recently, try again in retry seconds
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
