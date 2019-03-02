//
// "$Id: tiny.h 5569 2019-01-12 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.h has function declarations for all .c files.
//
// Copyright 2018-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm/issues/new
//
#include <stdio.h>
#include <sys/stat.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

#define TERMLINES	25
#define TERMCOLS	80
#define MAXLINES	8192
#define BUFFERSIZE	8192*64

struct Tunnel
{
	int socket;
	char *localip;
	char *remoteip;
	unsigned short localport;
	unsigned short remoteport;
	LIBSSH2_CHANNEL *channel;
	struct Tunnel *next;
	struct tagHOST *host;
};

typedef struct tagHOST {
	int host_type;
	int host_status;

	char cmdline[256];
	SOCKET sock;					//for tcp connection used by telnet/ssh reader
	HANDLE hExitEvent, hSerial;		//for serial reader
	HANDLE hStdioRead, hStdioWrite;	//for stdio reader
	HANDLE hReaderThread;			//reader thread handle

	short port;						//ssh/sftp/netconf host
	char *subsystem;
	char *username;
	char *password;
	char *passphrase;
	char *hostname;
	char homedir[MAX_PATH];
	char keys[256];
	int cursor, bReturn, bPassword;
	HANDLE mtx;						//ssh2 reading/writing mutex
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	HANDLE mtx_tun;					//tunnel list add/delete mutex
	struct Tunnel *tunnel_list;

	LIBSSH2_SFTP *sftp;				//sftp host
	char homepath[MAX_PATH];
	char realpath[MAX_PATH];
	int msg_id;						//netconf host

	struct tagTERM *term;
} HOST;

typedef struct tagTERM {
	char buff[BUFFERSIZE], attr[BUFFERSIZE], c_attr;
	int line[MAXLINES];
	int size_x, size_y;
	int cursor_x, cursor_y;
	int screen_y, scroll_y;
	int sel_left, sel_right;
	BOOL bLogging, bEcho, bCursor, bAlterScreen;
	BOOL bAppCursor, bGraphic, bEscape, bTitle, bInsert;
	int save_x, save_y;
	int roll_top, roll_bot;

	char title[64];
	int title_idx;
	FILE *fpLogFile;

	BOOL bPrompt;
	char sPrompt[32];
	int  iPrompt, iTimeOut;
	char *tl1text;
	int tl1len;

	BOOL save_edit;
	char escape_code[32];
	int escape_idx;

	int xmlIndent;
	BOOL xmlPreviousIsOpen;

	HOST *host;
} TERM;
#define HOST_IDLE			0
#define HOST_CONNECTING 	1
#define HOST_AUTHENTICATING	2
#define HOST_CONNECTED		4

#define STDIO  	1
#define SERIAL 	2
#define TELNET 	3
#define SSH		4
#define SFTP	5
#define NETCONF	6

BOOL isUTF8c(char c);	//check if a byte is a UTF8 continuation byte
FILE *fopen_utf8(const char *fn, const char *mode);
int stat_utf8(const char *fn, struct _stat *buffer);

/****************auto_drop.c*************/
void drop_Init( HWND hwnd, void (*handler)(char *) );
void drop_Destroy( HWND hwnd );
int autocomplete_Init( HWND hWnd );
int autocomplete_Destroy( );
int autocomplete_Add( WCHAR *cmd );
WCHAR *autocomplete_Prev( );
WCHAR *autocomplete_First( );
WCHAR *autocomplete_Next( );

/****************host.c****************/
void host_Init( HOST *ph );
void host_Open( HOST *ph, char *port );
void host_Send_Size( HOST *ph, int w, int h );
void host_Send( HOST *ph, char *buf, int len );
int host_Type(HOST *ph);
int host_Status(HOST *ph);
void host_Close( HOST *ph );
SOCKET tcp( char *host, short port );

/****************ssh2.c****************/
void ssh2_Init( HOST *ph );
void ssh2_Size( HOST *ph, int w, int h );
void ssh2_Send( HOST *ph, char *buf, int len );
char *ssh2_Gets( HOST *ph, char *prompt, BOOL bEcho);
void ssh2_Close( HOST *ph );
void ssh2_Tun( HOST *ph, char *cmd );
void scp_read( HOST *ph, char *lpath, char *rfiles );
void scp_write( HOST *ph, char *lpath, char *rpath );
void netconf_Send( HOST *ph, char *msg, int len );
int sftp_cmd( HOST *ph, char *cmd );

/****************ftpd.c****************/
BOOL ftp_Svr( char *root );
BOOL tftp_Svr( char *root );

/****************term.c****************/
void host_callback( void *term, char *buf, int len);
void term_Init( TERM *pt );
void term_Clear( TERM *pt );
void term_Size( TERM *pt, int x, int y );
void term_Title( TERM *pt, char *title );
void term_Parse( TERM *pt, char *buf, int len );
void term_Parse_XML( TERM *pt, char *xml, int len );
void term_Print( TERM *pt, const char *fmt, ... );
void term_Scroll( TERM *pt, int lines);
void term_Keydown( TERM *pt, DWORD key );

BOOL term_Echo( TERM *pt );
void term_Logg( TERM *pt, char *fn );
void term_Srch( TERM *pt, char *sstr );
void term_Disp( TERM *pt, char *buf );
void term_Send( TERM *pt, char *buf, int len );
int term_Recv( TERM *pt, char **preply );	//get new text since last Disp/Send/Recv
void term_Learn_Prompt( TERM *pt );
char *term_Mark_Prompt( TERM *pt );
int term_Waitfor_Prompt(TERM *pt );
int term_Pwd( TERM *pt, char *buf, int len );
int term_Scp( TERM *pt, char *cmd, char **preply );
int term_Tun( TERM *pt, char *cmd, char **preply );
int term_Cmd( TERM *pt, char *cmd, char **preply );

/****************tiny.c****************/
void cmd_Disp_utf8(char *buf);
void tiny_Beep();
void tiny_Redraw();
void tiny_Connecting();
void tiny_Title( char *buf );
BOOL tiny_Edit(BOOL e);			//return BOOL to indicate if status changed
char *tiny_Hostname();			//return hostname of current connection
char *tiny_Gets(char *prompt, BOOL bEcho);
void url_decode(char *url);
int http_Svr( char *intf );
