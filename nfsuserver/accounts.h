#ifndef ACCOUNTS_H
#define ACCOUNTS_H

// Accounts and personas live in an SQLite database (nfsu.db in the working
// directory). Name rules follow the game's own messages: account names 4-16,
// personas 3-12, passwords 4-16 characters.

enum { ACC_OK, ACC_NOUSER, ACC_WRONG, ACC_BADPASS, ACC_BANNED, ACC_LOCKED, ACC_THROTTLED, ACC_DUPL, ACC_MAXP, ACC_ERROR };

bool DbOpen( const char * path );
void DbClose();

// letters and digits only, as the game can type no others; between min and max characters
bool NameValid( const char * name, size_t min, size_t max );
// PASS= as the game sends it: "~" + 30 encoded bytes, maybe quoted and %XX escaped
bool DecodePass( const char * field, char * out, size_t size );

// ip is where the login comes from; name receives the account name as stored,
// personas up to 4 names
int AccountLogin( const char * given, const char * pass, const char * ip, char * name, char personas[4][100] );
int AccountCreate( const char * name, const char * pass );
int PersonaCreate( const char * account, const char * persona );
bool PersonaDelete( const char * account, const char * persona );
bool LoadPersonas( const char * account, char personas[4][100] );

#endif
