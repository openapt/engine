#include <r_socket.h>
#define MAX_LINE 2048
//#define SERVER "www.openssl.org"
#define PORT "4433"
#define SERVER "127.0.0.1"

int main (int argc, char ** argv) {
	ut8 buf [MAX_LINE+1];

	memset (buf, 0, MAX_LINE+1);
	RSocket *s = r_socket_new (R_TRUE);
	if (s == NULL) {
		fprintf (stderr, "Error, cannot create new socket \n");
		return 1;
	}
	if (!r_socket_connect_tcp (s, SERVER, PORT)) {
		fprintf (stderr, "Error, cannot connect to "SERVER"\n");
		return 1;
	}
	printf ("%i\n",r_socket_puts (s, "GET /\r\n\r\n"));
	while(r_socket_read (s, buf, MAX_LINE)>0)
		printf ("%s", buf);
	r_socket_free (s);
	return 0;
}
