#include "win_nix.h"
#include "objects.h"
#include "accounts.h"

extern void Log(char* log);
extern bool Verbose;

extern char* arr[30];
extern char arr2[30][1024];

extern ServerClass Server;

extern std::vector<PlayerStat> PS;

extern char tmpPlayerStat[1000];

extern SessionsClass Sessions;

UserClass::UserClass() {
	memset(Username, 0, sizeof(Username));
	memset(Personas, 0, sizeof(Personas));
	memset(IP, 0, 20);
	memset(Port, 0, 10);
	SelectedPerson = -1;
	Idle = 0;
};

ServerClass::ServerClass() {
	time(&Startup);
};

UsersClass::UsersClass() {
	cid = 1;
	Count = 0;
	First = NULL;
};

void UserClass::SelectPerson(char* person) {
	int k;
	k = 0;

	while (Personas[k][0] != 0) {
		if (strcmp(Personas[k], person) == 0) {
			SelectedPerson = k;
			return;
		}
		k++;
	}
};

UserClass* UsersClass::UserFromUsername(char* username) {
	UserClass* tmp;
	tmp = First;

	while (tmp != NULL) {
		if ((strcmp(tmp->Username, username) == 0) || (strcmp(tmp->Personas[tmp->SelectedPerson], username) == 0)) {
			return tmp;
		}
		tmp = tmp->Next;
	}

	return NULL;
};

void UsersClass::AddUser(UserClass* user) {
	memset(user->car, 0, 1024);
	user->Game = NULL;

	if (user->id == -1) user->id = cid++;

	Count++;
	user->Next = NULL;
	user->Idle = 0;

	if (First == NULL) {
		First = user;
		return;
	}

	UserClass* tmp;
	tmp = First;

	while (tmp->Next != NULL)
		tmp = tmp->Next;

	tmp->Next = user;
}

RUsersClass::RUsersClass() {
	First = NULL;
	Count = 0;
}

void RUsersClass::AddUser(RUserClass* user) {
	Count++;
	user->Next = NULL;

	if (First == NULL) {
		First = user;
		return;
	}

	RUserClass* tmp;
	tmp = First;

	while (tmp->Next != NULL)
		tmp = tmp->Next;

	tmp->Next = user;
}

void RUsersClass::RemoveUser(RUserClass* user) {
	if (Count == 1) {
		First = NULL;
	}
	else {
		if (First == user) {
			First = First->Next;
		}
		else {
			RUserClass* tmp, * prev;
			tmp = First;
			prev = tmp;

			while ((tmp != user) && (tmp != NULL)) {
				prev = tmp;
				tmp = tmp->Next;
			}
			if (tmp != NULL) {
				prev->Next = tmp->Next;
			}
		}
	}

	Count--;
}

void UsersClass::RemoveUser(UserClass* user) {
	if (Count == 1) {
		First = NULL;
	}
	else {
		if (First == user) {
			First = First->Next;
		}
		else {
			UserClass* tmp, * prev;
			tmp = First;
			prev = tmp;

			while ((tmp != user) && (tmp != NULL)) {
				prev = tmp;
				tmp = tmp->Next;
			}
			if (tmp != NULL) {
				prev->Next = tmp->Next;
			}
		}
	}

	Count--;
}

void ServerClass::SendRoomsToUser(UserClass* user, char* buffer) {
	RoomClass* tmp;
	tmp = Rooms.First;
	while (tmp != NULL) {
		if ((tmp->Count > 0) || (tmp->IsGlobal)) {
			sprintf(arr2[0], "I=%u", tmp->ID);
			sprintf(arr2[1], "N=%s", tmp->Name);
			sprintf(arr2[2], "H=3PriedeZ");
			sprintf(arr2[3], "F=CK");
			sprintf(arr2[4], "T=%u", tmp->Count);
			sprintf(arr2[5], "L=50");
			sprintf(arr2[6], "P=0");
			if (user->Connection != NULL)
				user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "+rom", arr, 7));
		}
		else {
			sprintf(buffer, "Error:Room empty\n");
			if (Verbose) Log(buffer);
		}
		tmp = tmp->Next;
	}
};

RoomsClass::RoomsClass() {
	cid = 1;
	Count = 0;
}

void BroadCastCommand(RUsersClass& users, char* command, char* params[], int paramcount, char* buffer) {
	RUserClass* tmp = users.First;

	while (tmp != NULL) {
		if (tmp->User->Connection != NULL) {
			tmp->User->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, command, params, paramcount));
		}
		tmp = tmp->Next;
	}
};

void BroadCastCommand(UsersClass* users, char* command, char* params[], int paramcount, char* buffer) {
	UserClass* tmp = users->First;

	while (tmp != NULL) {
		if (tmp->Connection != NULL) {
			tmp->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, command, params, paramcount));
		}
		tmp = tmp->Next;
	}
};

/*
* Which address of a player to hand out.
*
* The games of the players talk to each other directly, and the server tells
* them where to find each other. All it knows about a player is the address the
* connection to the server came from, and that address is of no use to a player
* in another network: someone playing on the machine of the server shows up as
* 127.0.0.1, and someone in the LAN of the server with a private address that
* nobody on the internet can reach. So the address depends on who it is for.
*/

// 127.0.0.0/8, the private networks (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16),
// link local (169.254.0.0/16) and the carrier NAT range (100.64.0.0/10) that
// VPNs like Tailscale use as well. addr is in network byte order.
static bool IsInternal(uint32_t addr) {
	uint32_t a = ntohl(addr);

	return (a >> 24) == 127 || (a >> 24) == 10 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8
		|| (a >> 16) == 0xA9FE || (a >> 22) == 0x191;
};

// A connection from the machine of the server itself: it comes over the loopback
// device, or it has the same address on both ends.
static bool IsOnServer(ConnectionClass* con) {
	return con != NULL && (con->remote_ip.sin_addr.s_addr == con->local_ip.sin_addr.s_addr
		|| (ntohl(con->remote_ip.sin_addr.s_addr) >> 24) == 127);
};

// A player who is being removed has no connection anymore, so the loopback
// device has to be recognised without one.
static bool IsOnServer(UserClass* user) {
	return (ntohl(inet_addr(user->IP)) >> 24) == 127 || IsOnServer(user->Connection);
};

// The address the redirector sends a game to for the lobby: serverexternalip if
// it is set, otherwise the address the game has just reached the redirector on.
// A game on the machine of the server always gets the latter. It has no business
// going out to the router and back in, which not every router does.
const char* LobbyAddrFor(ConnectionClass* con) {
	if (Server.ServerExternalIP[0] != '\0' && !IsOnServer(con)) return Server.ServerExternalIP;

	return inet_ntoa(con->local_ip.sin_addr);
};

// The address of the player `about` as the player `to` has to use it.
const char* AddrFor(UserClass* about, UserClass* to) {
	if (to == NULL || to == about) return about->IP;

	bool toInside = IsOnServer(to) || IsInternal(inet_addr(to->IP));

	if (IsOnServer(about)) {
		if (IsOnServer(to)) return about->IP;

		// everybody else finds this player where they find the server
		if (!toInside && Server.ServerExternalIP[0] != '\0') return Server.ServerExternalIP;
		if (to->Connection != NULL) return inet_ntoa(to->Connection->local_ip.sin_addr);

		return about->IP;
	}

	// a public address is good for everybody, and players inside reach each other directly
	if (!IsInternal(inet_addr(about->IP)) || toInside) return about->IP;

	// `about` is inside and `to` is not: hand out the public address of the server
	if (Server.ServerExternalIP[0] != '\0') return Server.ServerExternalIP;
	if (to->Connection != NULL && !IsInternal(to->Connection->local_ip.sin_addr.s_addr))
		return inet_ntoa(to->Connection->local_ip.sin_addr);

	return about->IP;
};

// BroadCastCommand() for a message in which parameter `addrparam` is the address
// ("A=...") of the player `about`: every receiver gets the one meant for it.
void BroadCastCommand(RUsersClass& users, char* command, char* params[], int paramcount, char* buffer, UserClass* about, int addrparam) {
	RUserClass* tmp = users.First;

	while (tmp != NULL) {
		if (tmp->User->Connection != NULL) {
			sprintf(params[addrparam], "A=%s", AddrFor(about, tmp->User));
			tmp->User->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, command, params, paramcount));
		}
		tmp = tmp->Next;
	}

	sprintf(params[addrparam], "A=%s", about->IP);
};

void BroadCastCommand(UsersClass* users, char* command, char* params[], int paramcount, char* buffer, UserClass* about, int addrparam) {
	UserClass* tmp = users->First;

	while (tmp != NULL) {
		if (tmp->Connection != NULL) {
			sprintf(params[addrparam], "A=%s", AddrFor(about, tmp));
			tmp->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, command, params, paramcount));
		}
		tmp = tmp->Next;
	}

	sprintf(params[addrparam], "A=%s", about->IP);
};

// Sets the addresses of the players of this game (ADDR0, ADDR1, ... in the
// parameters of a message about the game) as the player `to` has to use them.
void GameClass::FillAddresses(UserClass* to) {
	int l = 0;

	for (RUserClass* ru = Users.First; ru != NULL && 13 + l * 3 < 30; ru = ru->Next) {
		sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, AddrFor(ru->User, to));
		l++;
	}
};

// BroadCastCommand() for a message about this game
void GameClass::BroadCast(RUsersClass& users, char* command, int paramcount, char* buffer) {
	RUserClass* tmp = users.First;

	while (tmp != NULL) {
		if (tmp->User->Connection != NULL) {
			FillAddresses(tmp->User);
			tmp->User->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, command, arr, paramcount));
		}
		tmp = tmp->Next;
	}

	FillAddresses(NULL);
};

void RoomsClass::AddRoom(RoomClass* room) {
	Count++;
	room->Next = NULL;
	room->Count = 0;
	room->ID = cid++;

	if (First == NULL) {
		First = room;
		return;
	}

	RoomClass* tmp;
	tmp = First;

	while (tmp->Next != NULL)
		tmp = tmp->Next;

	tmp->Next = room;
}

void RoomsClass::RemoveRoom(RoomClass* room) {
	if (Count == 1) {
		First = NULL;
	}
	else {
		if (First == room) {
			First = First->Next;
		}
		else {
			RoomClass* tmp, * prev;
			tmp = First;
			prev = tmp;

			while ((tmp != room) && (tmp != NULL)) {
				prev = tmp;
				tmp = tmp->Next;
			}
			if (tmp != NULL) {
				prev->Next = tmp->Next;
			}
		}
	}

	Count--;
	room->Name[0] = 0;
}

void SessionsClass::AddSession(SessionClass* session) {
	Count++;
	session->Next = NULL;

	if (First == NULL) {
		First = session;
		return;
	}
	SessionClass* tmp;
	tmp = First;
	while (tmp->Next != NULL) tmp = tmp->Next;
	tmp->Next = session;
};

void SessionsClass::RemoveSession(SessionClass* session) {
	if (Count == 1) {
		First = NULL;
	}
	else {
		if (First == session) {
			First = First->Next;
		}
		else {
			SessionClass* tmp, * prev;
			tmp = First;
			prev = tmp;
			while ((tmp != session) && (tmp != NULL)) {
				prev = tmp;
				tmp = tmp->Next;
			}
			if (tmp != NULL) {
				prev->Next = tmp->Next;
			}
		}
	}
	Count--;
};

SessionsClass::SessionsClass() {
	First = 0;
	Count = 0;
};


GameClass* GamesClass::GameFromName(char* name) {
	GameClass* tmp;
	tmp = First;

	while (tmp != NULL) {
		if (strcmp(tmp->Name, name) == 0) {
			return tmp;
		}
		tmp = tmp->Next;
	}

	return NULL;
};

void GamesClass::AddGame(GameClass* game) {
	Count++;

	if (cid < 1) cid = 1;

	game->Next = NULL;
	game->Count = 0;
	game->ID = cid++;

	if (First == NULL) {
		First = game;
		return;
	}

	GameClass* tmp;
	tmp = First;

	while (tmp->Next != NULL)
		tmp = tmp->Next;

	tmp->Next = game;
}

void GamesClass::RemoveGame(GameClass* game) {
	if (Count == 1) {
		First = NULL;
	}
	else {
		if (First == game) {
			First = First->Next;
		}
		else {
			GameClass* tmp, * prev;
			tmp = First;
			prev = tmp;

			while ((tmp != game) && (tmp != NULL)) {
				prev = tmp;
				tmp = tmp->Next;
			}
			if (tmp != NULL) {
				prev->Next = tmp->Next;
			}
		}
	}

	Count--;
	game->Name[0] = 0;
}

RoomClass* RoomsClass::RoomFromName(char* name) {
	RoomClass* tmp;
	tmp = First;

	while (tmp != NULL) {
		if (strcmp(tmp->Name, name) == 0) return tmp;
		tmp = tmp->Next;
	}

	return NULL;
};

void GameClass::SendInfoToUser(UserClass* user, char* buffer) {
	sprintf(buffer, "GameClass::SendInfoToUser\n");

	if (Verbose) Log(buffer);

	if (Users.Count == 0) {
		sprintf(buffer, "Error: no users in game\n");

		if (Verbose) Log(buffer);
		return;
	}

	int k;

	sprintf(arr2[0], "IDENT=%u", ID);
	sprintf(arr2[1], "WHEN=2003.12.8 15:52:54");
	sprintf(arr2[2], "NAME=%s", Name);
	sprintf(arr2[3], "HOST=%s", Users.First->User->Personas[Users.First->User->SelectedPerson]);
	sprintf(arr2[4], "PARAMS=%s", params);
	sprintf(arr2[5], "ROOM=%s", user->CurrentRoom->Name);
	sprintf(arr2[6], "MAXSIZE=%u", max);
	sprintf(arr2[7], "MINSIZE=%u", min);
	sprintf(arr2[8], "COUNT=%u", Count);
	sprintf(arr2[9], "USERFLAGS=0");
	sprintf(arr2[10], "SYSFLAGS=%u", sysflags);

	k = rand();
	int l = 0;
	int m = 0;
	RUserClass* ru = Users.First;

	while (ru != NULL) {
		sprintf(arr2[11 + l * 3], "OPID%u=%u", l, ru->User->id);
		sprintf(arr2[12 + l * 3], "OPPO%u=%s", l, ru->User->Personas[ru->User->SelectedPerson]);
		sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, ru->User->IP);
		l++;
		ru = ru->Next;
	}

	FillAddresses(user);

	if (user->Connection != NULL)
		user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "+agm", arr, 11 + l * 3));
};

void GameClass::RemoveUser(UserClass* user, char* buffer) {
	RUserClass* tmp;
	tmp = Users.First;

	while (tmp != NULL) {
		if (tmp->User == user) {
			Users.RemoveUser(tmp);
			Count--;
			sprintf(buffer, "User removed - %u users in game\n", Count);

			if (Verbose) Log(buffer);

			user->Game = NULL;

			int k;

			if (Count > 0) {
				sprintf(arr2[0], "IDENT=%u", ID);
				sprintf(arr2[1], "WHEN=2003.12.8 15:52:54");
				sprintf(arr2[2], "NAME=%s", Name);
				sprintf(arr2[3], "HOST=%s", Users.First->User->Personas[Users.First->User->SelectedPerson]);
				sprintf(arr2[4], "PARAMS=%s", params);
				sprintf(arr2[5], "ROOM=%s", user->CurrentRoom->Name);
				sprintf(arr2[6], "MAXSIZE=%u", max);
				sprintf(arr2[7], "MINSIZE=%u", min);
				sprintf(arr2[8], "COUNT=%u", Count);
				sprintf(arr2[9], "USERFLAGS=0");
				sprintf(arr2[10], "SYSFLAGS=%u", sysflags);

				k = rand();
				int l = 0;
				int m = 0;
				RUserClass* ru = Users.First;

				while (ru != NULL) {
					sprintf(arr2[11 + l * 3], "OPID%u=%u", l, ru->User->id);
					sprintf(arr2[12 + l * 3], "OPPO%u=%s", l, ru->User->Personas[ru->User->SelectedPerson]);
					sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, ru->User->IP);
					l++;
					ru = ru->Next;
				}

				if (user->Connection != NULL) {
					FillAddresses(user);
					user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "glea", arr, 11 + l * 3));
					user->CurrentRoom->RefreshUser(user, buffer);
				}

				if (max == Users.Count) {
					StartGame(buffer);
				}

				sprintf(arr2[0], "IDENT=%u", ID);
				sprintf(arr2[1], "WHEN=2003.12.8 15:52:54");
				sprintf(arr2[2], "NAME=%s", Name);
				sprintf(arr2[3], "HOST=%s", Users.First->User->Personas[Users.First->User->SelectedPerson]);
				sprintf(arr2[4], "PARAMS=%s", params);
				sprintf(arr2[5], "ROOM=%s", user->CurrentRoom->Name);
				sprintf(arr2[6], "MAXSIZE=%u", max);
				sprintf(arr2[7], "MINSIZE=%u", min);
				sprintf(arr2[8], "COUNT=%u", Count);
				sprintf(arr2[9], "USERFLAGS=0");
				sprintf(arr2[10], "SYSFLAGS=%u", sysflags);

				l = 0;
				m = 0;
				ru = Users.First;

				while (ru != NULL) {
					sprintf(arr2[11 + l * 3], "OPID%u=%u", l, ru->User->id);
					sprintf(arr2[12 + l * 3], "OPPO%u=%s", l, ru->User->Personas[ru->User->SelectedPerson]);
					sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, ru->User->IP);
					l++;
					ru = ru->Next;
				}

				BroadCast(user->CurrentRoom->Users, "+agm", 11 + l * 3, buffer);
			}
			else {
				sprintf(arr2[0], "IDENT=%u", ID);
				BroadCastCommand(user->CurrentRoom->Users, "+agm", arr, 1, buffer);
			}

			free(tmp);
			tmp = NULL;

			return;
		}
		else {
			tmp = tmp->Next;
		}
	}
};

void GameClass::AddUser(UserClass* user, char* buffer) {
	sprintf(buffer, "Add user\n");

	if (Verbose) Log(buffer);

	user->Game = this;
	Count++;
	RUserClass* tm = (RUserClass*)calloc(1, sizeof(RUserClass));
	tm->User = user;
	Users.AddUser(tm);

	int k;

	sprintf(arr2[0], "IDENT=%u", ID);
	sprintf(arr2[1], "WHEN=2003.12.8 15:52:54");
	sprintf(arr2[2], "NAME=%s", Name);
	sprintf(arr2[3], "HOST=%s", Users.First->User->Personas[Users.First->User->SelectedPerson]);
	sprintf(arr2[4], "PARAMS=%s", params);
	sprintf(arr2[5], "ROOM=%s", user->CurrentRoom->Name);
	sprintf(arr2[6], "MAXSIZE=%u", max);
	sprintf(arr2[7], "MINSIZE=%u", min);
	sprintf(arr2[8], "COUNT=%u", Count);
	sprintf(arr2[9], "USERFLAGS=0");
	sprintf(arr2[10], "SYSFLAGS=%u", sysflags);

	k = rand();
	int l = 0;
	int m = 0;
	RUserClass* ru = Users.First;

	while (ru != NULL) {
		sprintf(arr2[11 + l * 3], "OPID%u=%u", l, ru->User->id);
		sprintf(arr2[12 + l * 3], "OPPO%u=%s", l, ru->User->Personas[ru->User->SelectedPerson]);
		sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, ru->User->IP);
		l++;
		ru = ru->Next;
	}

	FillAddresses(user);

	if (user->Connection) user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "gjoi", arr, 11 + l * 3));

	user->CurrentRoom->RefreshUser(user, buffer);

	if (max == Users.Count) {
		StartGame(buffer);
	}


	sprintf(arr2[0], "IDENT=%u", ID);
	sprintf(arr2[1], "WHEN=2003.12..8 15:52:54");
	sprintf(arr2[2], "NAME=%s", Name);
	sprintf(arr2[3], "HOST=%s", Users.First->User->Personas[Users.First->User->SelectedPerson]);
	sprintf(arr2[4], "PARAMS=%s", params);
	sprintf(arr2[5], "ROOM=%s", user->CurrentRoom->Name);
	sprintf(arr2[6], "MAXSIZE=%u", max);
	sprintf(arr2[7], "MINSIZE=%u", min);
	sprintf(arr2[8], "COUNT=%u", Count);
	sprintf(arr2[9], "USERFLAGS=0");
	sprintf(arr2[10], "SYSFLAGS=%u", sysflags);

	l = 0;
	m = 0;
	ru = Users.First;

	while (ru != NULL) {
		sprintf(arr2[11 + l * 3], "OPID%u=%u", l, ru->User->id);
		sprintf(arr2[12 + l * 3], "OPPO%u=%s", l, ru->User->Personas[ru->User->SelectedPerson]);
		sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, ru->User->IP);
		l++;
		ru = ru->Next;
	}
	BroadCast(user->CurrentRoom->Users, "+agm", 11 + l * 3, buffer);
}

void GameClass::StartGame(char* buffer) {
	working = true; // the race is on: the JSON API lists it as started
	sprintf(buffer, "start game\n");

	if (Verbose) Log(buffer);

	int k;

	sprintf(arr2[0], "IDENT=%u", ID);
	sprintf(arr2[1], "WHEN=2003.12.8 15:52:54");
	sprintf(arr2[2], "NAME=%s", Name);
	sprintf(arr2[3], "HOST=%s", Users.First->User->Personas[Users.First->User->SelectedPerson]);
	sprintf(arr2[4], "PARAMS=%s", params);
	sprintf(arr2[5], "ROOM=%s", Users.First->User->CurrentRoom->Name);
	sprintf(arr2[6], "MAXSIZE=%u", max);
	sprintf(arr2[7], "MINSIZE=%u", min);
	sprintf(arr2[8], "COUNT=%u", Count);
	sprintf(arr2[9], "USERFLAGS=0");
	sprintf(arr2[10], "SYSFLAGS=%u", sysflags);

	k = rand();
	int l = 0;
	int m = 0;
	RUserClass* ru = Users.First;

	while (ru != NULL) {
		sprintf(arr2[11 + l * 3], "OPID%u=%u", l, ru->User->id);
		sprintf(arr2[12 + l * 3], "OPPO%u=%s", l, ru->User->Personas[ru->User->SelectedPerson]);
		sprintf(arr2[13 + l * 3], "ADDR%u=%s", l, ru->User->IP);
		l++;
		ru = ru->Next;
	}

	ru = Users.First;

	SessionClass* session;

	while (ru != NULL) {
		FillAddresses(ru->User);
		sprintf(arr2[11 + l * 3], "SEED=%u", k);
		sprintf(arr2[12 + l * 3], "SELF=%s", ru->User->Personas[ru->User->SelectedPerson]);

		if (ru->User->Connection != NULL) {
			/*
			* Add info about started sessions (a session = a race).
			* Need to save information about the player's name and room type in order to determine
			* there was a rating race or not.
			*/
			// one session per persona: a race quit without a report must not decide the next one
			const char* persona = ru->User->Personas[ru->User->SelectedPerson];
			for (session = Sessions.First; session != NULL; session = session->Next)
				if (stricmp(session->Persona, persona) == 0) break;
			if (session == NULL) {
				session = (SessionClass*)calloc(1, sizeof(SessionClass));
				snprintf(session->Persona, sizeof(session->Persona), "%s", persona);
				Sessions.AddSession(session);
			}
			snprintf(session->FromRoom, sizeof(session->FromRoom), "%s", Users.First->User->CurrentRoom->Name);
			snprintf(session->IP, sizeof(session->IP), "%s", ru->User->IP);
			session->Idle = 0;
			ru->User->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "+ses", arr, 13 + l * 3));
		}

		ru = ru->Next;
	}

	BroadCast(Users.First->User->CurrentRoom->Users, "+agm", 11 + l * 3, buffer);
}

void RoomClass::AddUser(UserClass* user, char* buffer) {
	if (Verbose)
		Log("Add user\n");

	sprintf(arr2[0], "IDENT=%u", ID);
	sprintf(arr2[1], "NAME=%s", Name);
	sprintf(arr2[2], "COUNT=%u", Users.Count);
	sprintf(arr2[3], "FLAGS=C");

	if (user->Connection != NULL) user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "move", arr, 4));

	user->CurrentRoom = this;
	Count++;
	RUserClass* tm = (RUserClass*)calloc(1, sizeof(RUserClass));
	tm->User = user;

	Users.AddUser(tm);

	sprintf(buffer, "User added - %u users in room\n", Count);

	if (Verbose) Log(buffer);

	sprintf(arr2[0], "I=%u", user->id);
	sprintf(arr2[1], "N=%s", user->Personas[user->SelectedPerson]);
	sprintf(arr2[2], "M=%s", user->Username);
	sprintf(arr2[3], "F=");
	sprintf(arr2[4], "A=%s", user->IP);
	sprintf(arr2[5], "S=%s", GetPlayerStat(user->Personas[user->SelectedPerson]));
	sprintf(arr2[6], "X=%s", user->car);
	sprintf(arr2[7], "R=%s", Name);
	sprintf(arr2[8], "RI=%u", ID);
	sprintf(arr2[9], "RF=C");
	sprintf(arr2[10], "RT=1");

	if (user->Connection != NULL) user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "+who", arr, 11));

	RefreshUser(user, buffer);

	GameClass* game = Games.First;

	while (game != NULL) {
		game->SendInfoToUser(user, buffer);
		game = game->Next;
	}

	ListToUser(user, buffer);
};

void RoomClass::RefreshUser(UserClass* user, char* buffer) {
	sprintf(buffer, "RoomClass::RefreshUser\n");

	if (Verbose) Log(buffer);

	sprintf(arr2[0], "I=%u", user->id);
	sprintf(arr2[1], "N=%s", user->Personas[user->SelectedPerson]);
	sprintf(arr2[2], "M=%s", user->Username);
	sprintf(arr2[3], "F=H");
	sprintf(arr2[4], "A=%s", user->IP);
	sprintf(arr2[5], "P=211");
	sprintf(arr2[6], "S=%s", GetPlayerStat(user->Personas[user->SelectedPerson]));
	sprintf(arr2[7], "X=%s", user->car);

	if (user->Game != NULL) {
		sprintf(arr2[8], "G=%u", user->Game->ID);
	}
	else {
		sprintf(arr2[8], "G=0");
	}

	sprintf(arr2[9], "T=2");

	BroadCastCommand(Users, "+usr", arr, 10, buffer, user, 4);
};

void RoomClass::RemoveUser(UserClass* user, char* buffer) {
	RUserClass* tmp;
	tmp = Users.First;

	while (tmp != NULL) {
		if (tmp->User == user) {
			Users.RemoveUser(tmp);
			user->CurrentRoom = NULL;
			Count--;
			sprintf(buffer, "User removed - %u users in room\n", Count);

			if (Verbose) Log(buffer);

			sprintf(arr2[0], "I=%u", user->id);
			sprintf(arr2[1], "T=1");

			BroadCastCommand(Users, "+usr", arr, 2, buffer);

			sprintf(arr2[0], "F=C");
			sprintf(arr2[1], "T=\"has left the room\"");
			sprintf(arr2[2], "N=%s", user->Personas[user->SelectedPerson]);

			BroadCastCommand(Users, "+msg", arr, 3, buffer);

			free(tmp);
			tmp = NULL;
			return;
		}
		else {
			tmp = tmp->Next;
		}
	}
};

void RoomClass::ListToUser(UserClass* user, char* buffer) {
	RUserClass* tmp;
	tmp = Users.First;
	sprintf(buffer, "Sending list\n");

	if (Verbose) Log(buffer);

	while (tmp != NULL) {
		sprintf(arr2[0], "I=%u", tmp->User->id);
		sprintf(arr2[1], "N=%s", tmp->User->Personas[tmp->User->SelectedPerson]);
		sprintf(arr2[2], "M=%s", tmp->User->Username);
		sprintf(arr2[3], "F=H");
		sprintf(arr2[4], "A=%s", AddrFor(tmp->User, user));
		sprintf(arr2[5], "P=211");
		sprintf(arr2[6], "S=%s", GetPlayerStat(tmp->User->Personas[tmp->User->SelectedPerson]));
		sprintf(arr2[7], "X=%s", tmp->User->car);

		if (tmp->User->Game != NULL) {
			sprintf(arr2[8], "G=%u", tmp->User->Game->ID);
		}
		else {
			sprintf(arr2[8], "G=0");
		}

		sprintf(arr2[9], "T=2");

		if (user->Connection != NULL)
			user->Connection->OutgoingMessages.AddMessage(MakeMessage(buffer, "+usr", arr, 10));
		tmp = tmp->Next;
	}
};



// PlayerStat holds its 35 numbers back to back after Name, in the order StatSave takes
typedef char PlayerStatLayout[sizeof (PlayerStat) == 16 + 35 * sizeof (int) ? 1 : -1];

void ServerClass::SaveStat(const char* persona) {
	std::vector<PlayerStat>::iterator it = std::find(PS.begin(), PS.end(), persona);
	if (it != PS.end()) StatSave(it->Name, &it->Rating_All);
};

static void AddStat(const char* persona, const int v[35]) {
	PlayerStat ps;
	snprintf(ps.Name, sizeof(ps.Name), "%s", persona);
	memcpy(&ps.Rating_All, v, 35 * sizeof(int));
	PS.push_back(ps);
}

void ServerClass::LoadStat() {
	PS.clear();
	StatLoadAll(AddStat);
};

char* GetPlayerStat(const char* Name) {
	//char tmp[1024];
	std::vector<PlayerStat>::iterator it;
	it = std::find(PS.begin(), PS.end(), Name);

	if (it != PS.end()) {
		sprintf(tmpPlayerStat, "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",
			it->Rating_All, it->Wins_All, it->Loses_All, it->Disc_All, it->REP_All, it->OppsREP_All, it->OppsRating_All,
			it->Rating_Circ, it->Wins_Circ, it->Loses_Circ, it->Disc_Circ, it->REP_Circ, it->OppsREP_Circ, it->OppsRating_Circ,
			it->Rating_Sprint, it->Wins_Sprint, it->Loses_Sprint, it->Disc_Sprint, it->REP_Sprint, it->OppsREP_Sprint, it->OppsRating_Sprint,
			it->Rating_Drag, it->Wins_Drag, it->Loses_Drag, it->Disc_Drag, it->REP_Drag, it->OppsREP_Drag, it->OppsRating_Drag,
			it->Rating_Drift, it->Wins_Drift, it->Loses_Drift, it->Disc_Drift, it->REP_Drift, it->OppsREP_Drift, it->OppsRating_Drift);
		return tmpPlayerStat;
	}

	return NULL;
};


