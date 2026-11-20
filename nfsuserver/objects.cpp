#include "objects.h"

extern void Log(char *log);

void MessagesClass::AddMessage( MessageClass * msg ) {
	mut.Lock ();
	msg->Next = NULL;

	if (Count == 0) {
		First = msg;
		Last = msg;
	} else {
		Last->Next = msg;
		Last = msg;
	}

	Count++;
	mut.Unlock ();
} ;

MessageClass *MessagesClass::RemoveFirstMessage() {
	mut.Lock ();

	if (Count > 0) {
		Count--;
		MessageClass * temp = First;
		First = First->Next;

		if (temp == Last) Last = NULL;

		mut.Unlock ();
		return temp;
	} else {
		mut.Unlock ();
		return NULL;
	}
} ;

void MessagesClass::Clear() {
	MessageClass * temp;

	while (Count > 0) {
		temp = RemoveFirstMessage ();
		free (temp->Message);
		free (temp);
	}
} ;

void ConnectionsClass::RemoveConnection( ConnectionClass * con ) {
	if (con == NULL) return;

	if (Count == 0) return;

	mut.Lock ();

	if (Count == 1) {
		First = NULL;
		Last = NULL;
		Count = 0;
	} else {
		if (First == con) {
			First = First->Next;
			Count--;
			mut.Unlock ();
			return;
		}

		ConnectionClass * prev, * temp;
		temp = First;
		prev = NULL;

		while ((temp != con) && (temp != NULL)) {
			prev = temp;
			temp = temp->Next;
		}
		if (prev != NULL) {
			if (temp == Last) {
				Last = prev;
				prev->Next = NULL;
			} else {
				prev->Next = temp->Next;
			}
			Count--;
		}
	}

	mut.Unlock ();
} ;

void ConnectionsClass::AddConnection( ConnectionClass * con ) {
	mut.Lock ();
	con->Next = NULL;

	if (Count == 0) {
		First = con;
		Last = con;
	} else {
		Last->Next = con;
		Last = con;
	}

	Count++;
	mut.Unlock ();
} ;

ConnectionsClass::ConnectionsClass() {
	mut.Init ();
	Count = 0;
	First = NULL;
} ;

MessageClass *MakeMessage( char * buffer, char * command, char * params [], int paramcount ) {
	int k = MakeCommand (buffer, command, params, paramcount);
	MessageClass * tmsg = (MessageClass *)calloc (1, sizeof (MessageClass));
	tmsg->Message = (char *)calloc (k, sizeof (char));
	memcpy (tmsg->Message, buffer, k);
	tmsg->Size = k;
	return tmsg;
} ;

// Copies the value of KEY= in a message body into out. Keys start a line;
// values end at a newline, a tab or the zero byte after the message. Returns
// false when the key is missing or its value does not fit into size bytes.
bool Field( const char * body, const char * key, char * out, size_t size ) {
	size_t keylen = strlen (key);

	for (const char * p = body; *p != 0; p++) {
		if ((p == body || p[-1] == '\n' || p[-1] == '\t') && strncmp (p, key, keylen) == 0 && p[keylen] == '=') {
			p += keylen + 1;
			size_t len = strcspn (p, "\n\t");
			if (len >= size) return false;
			memcpy (out, p, len);
			out[len] = 0;
			return true;
		}
	}

	return false;
} ;

int MakeCommand( char * buffer, char * command, char * params [], int paramcount ) {
	int len, k;
	memset (buffer, 0, 12);
	memcpy (buffer, command, strlen (command));
	buffer[11] = 12;
	len = 12;

	for (k = 0; k < paramcount; k++) {
		if(params[k]!=NULL){
			// every caller passes a 1024 byte buffer; later parameters are dropped
			if (len + strlen (params[k]) + 1 > 1024) break;
			memcpy (buffer + len, params[k], strlen (params[k]));
			len += (int)strlen (params[k]) + 1;
			buffer[len - 1] = 9;
			buffer[11] = (char)len;
			buffer[10] = (char)(len >> 8);
			buffer[9] = (char)(len >> 16);
			buffer[8] = (char)(len >> 24);
		}
	}

	if (paramcount > 0) {
		buffer[len - 1] = 0;
	}

	return len;
} ;
