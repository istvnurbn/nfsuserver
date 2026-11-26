#ifndef ACCOUNTS_H
#define ACCOUNTS_H

#include <string>
#include <vector>

// Accounts, personas, stats and track records live in an SQLite database
// (nfsu.db in the working directory). Name rules follow the game's own messages: account names 4-16,
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
int AccountSetPassword( const char * name, const char * pass );
int AccountDelete( const char * given, char * name, char personas[4][100] );
// created and lastLogin as SQLite writes them (UTC), personas comma separated
struct AccountInfo { std::string name, created, lastLogin, personas; };
bool AccountList( std::vector<AccountInfo> & out );
int PersonaCreate( const char * account, const char * persona );
bool PersonaDelete( const char * account, const char * persona );
bool LoadPersonas( const char * account, char personas[4][100] );

// Stats and track records. A race report is saved between DbBegin and DbCommit.
void DbBegin();
void DbCommit();

// v: the 35 numbers of a PlayerStat, 7 per mode (all, circuit, sprint, drag, drift)
bool StatSave( const char * persona, const int v[35] );
bool StatLoadAll( void (*each)( const char * persona, const int v[35] ) );

#define TRACK_COUNT 30
extern const int TRACKS[TRACK_COUNT];
struct TrackRecord { char persona[16]; int score, car, dir; };
// keeps the persona's best lap (ms) or, on tracks 1301-1308, most drift points
bool TrackRecordSave( int track, const char * persona, int score, int car, int dir );
int TrackRecordsTop( int track, TrackRecord * out, int max );

#endif
