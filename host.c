//
// "$Id: host.c 13434 2018-09-30 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// host.c is the host communication implementation 
// serial communication is based on WIN32 API.
// telnet communication is based on posix socket API
//
// Copyright 2015-2018 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//	   https://github.com/zoudaokou/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//	   https://github.com/zoudaokou/tinyTerm/issues/new
//
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fcntl.h>
#include "tiny.h"

int host_type = 0;
int host_status=CONN_IDLE;

static HANDLE hExitEvent, hWrite;	//for serial reader
static SOCKET sock;					//for telnet reader

DWORD dwReaderId = 0;				//reader thread id
DWORD WINAPI serial( void *pv );
DWORD WINAPI telnet( void *pv );
DWORD WINAPI ssh( void *pv );
DWORD WINAPI sftp( void *pv );
DWORD WINAPI stdio( void *pv );
DWORD WINAPI netconf( void *pv );
void stdio_Close( void );

extern BOOL bEcho;
unsigned char * telnet_Options( unsigned char *buf );

int host_Init( void )
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	ssh2_Init();
	return http_Svr("127.0.0.1");
}
void host_Open( char *port )
{
static char Port[256];

	LPTHREAD_START_ROUTINE reader = serial;
	if ( strncmp(port, "telnet", 6)==0 ) { port+=7; reader = telnet; }
	if ( strncmp(port, "ssh", 3)==0 )	 { port+=4; reader = ssh; }
	if ( strncmp(port, "sftp", 4)==0 )	 { port+=5; reader = sftp; }
	if ( strncmp(port, "netconf", 7)==0 ) { port+=8; reader= netconf; }
	if ( *port=='!' ) { port++; reader = stdio; }
	if ( dwReaderId==0 ) {
		strncpy(Port, port, 255); Port[255]=0;
		host_status=CONN_CONNECTING;
		tiny_Connecting();
		CreateThread( NULL,0,reader, Port,0,&dwReaderId );
	}
}
void host_Send( char *buf, int len )
{
	DWORD dwWrite;
	switch ( host_type ) {
	case STDIO: 
	case SERIAL:WriteFile(hWrite, buf, len, &dwWrite, NULL);
				break;
	case TELNET:send( sock, buf, len, 0);
				break;
	case SSH:
	case SFTP:
	case NETCONF: 
	default:	ssh2_Send(buf, len); break;
	}
}
void host_Size( int w, int h )
{
	if ( host_type==SSH ) ssh2_Size(w, h);
}
int host_Status()
{
	return host_status;
}
int host_Type()
{
	return host_type;
}
void host_Close( void )
{
	switch ( host_type ) {
	case STDIO:	 stdio_Close(); break;
	case SERIAL: SetEvent( hExitEvent ); break;
	case TELNET: closesocket( sock ); break;
	case SSH:
	case SFTP:
	case NETCONF:ssh2_Close(); break;
	}
}
void host_Destory( void )
{
	if ( host_type!=0 ) host_Close( );
	ssh2_Exit();
	http_Svr("127.0.0.1");
}

/***************************Serial*****************************/
DWORD WINAPI serial(void *pv)
{
	BOOL bConnected = TRUE;
	char port[256] = "\\\\.\\";
	strcat( port, (char *)pv );

	char *p = strchr( port, ':' );
	if ( p!=NULL ) *p++ = 0;
	if ( p==NULL || *p==0 ) p = "9600,n,8,1";

	HANDLE hCommPort = CreateFileA( port, GENERIC_READ|GENERIC_WRITE, 0, NULL,
														OPEN_EXISTING, 0, NULL);
	bConnected = (hCommPort!=INVALID_HANDLE_VALUE);
	if ( !bConnected ) 
		term_Disp("Couldn't open COM port\r\n");
	else {									//timeout and buffer settings
		COMMTIMEOUTS timeouts ={10,			//ReadIntervalTimeout = 10;
								 0,			//ReadTotalTimeoutMultiplier = 0;
								 1,			//ReadTotalTimeoutConstant = 1;
								 0,			//WriteTotalTimeoutMultiplier = 0;
								 0 };		//WriteTotalTimeoutConstant = 0;
			
		bConnected = (SetCommTimeouts(hCommPort,&timeouts)!=0); 
		if ( !bConnected ) term_Disp("couldn't set comm timeout\r\n");
		SetupComm( hCommPort, 4096, 1024 );	//comm buffer sizes
	}
	if ( bConnected ) {
		DCB dcb;							// comm port settings
		memset(&dcb, 0, sizeof(dcb));
		dcb.DCBlength = sizeof(dcb);
		BuildCommDCBA(p, &dcb);
		bConnected = (SetCommState(hCommPort, &dcb)!=0);
		if ( !bConnected ) 
			term_Disp("Invalid comm port settings\r\n" );
	}
	if ( bConnected ) {
		hWrite = hCommPort;
		host_status=CONN_CONNECTED;
		host_type = SERIAL;
		tiny_Title( (char *)pv );
		hExitEvent = CreateEventA( NULL, TRUE, FALSE, "COM exit" );
		while ( WaitForSingleObject( hExitEvent, 0 ) == WAIT_TIMEOUT ) { 
			char buf[256];
			DWORD dwCCH;
			if ( ReadFile(hCommPort, buf, 255, &dwCCH, NULL) ) {
				if ( dwCCH > 0 ) {
					buf[dwCCH] = 0;
					term_Parse( buf, dwCCH );
				}
			}
			else
				if ( !ClearCommError(hCommPort, NULL, NULL ) ) break;
		}
		CloseHandle(hExitEvent);
		tiny_Title("");
		host_type = 0;
	}
	CloseHandle(hCommPort);
	host_status=CONN_IDLE;
	dwReaderId = 0;
	return 1;
}

/***************Telnet*******************************/
int tcp(const char *host, short port)
{
	struct addrinfo *ainfo;	   
	if ( getaddrinfo(host, NULL, NULL, &ainfo)!=0 ) return -1;
	SOCKET s = socket(ainfo->ai_family, SOCK_STREAM, 0);
	((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(port);
	int rc = connect(s, ainfo->ai_addr, ainfo->ai_addrlen);
	freeaddrinfo(ainfo);
	if ( rc==SOCKET_ERROR ) {
		term_Disp( "connection failure!\r\n" );
		closesocket(s);
		return -1;
	}
	else
		return s;
}
DWORD WINAPI telnet( void *pv )
{
	short nPort = 23;
	char *port = (char *)pv;
	char *p=strchr(port, ':');
	if ( p!=NULL ){ *p++=0; nPort=atoi(p); }
	if ( (sock=tcp(port, nPort))==-1 ) goto socket_close;

	int cch;
	char buf[1536];
	host_status=CONN_CONNECTED;
	host_type=TELNET;
	tiny_Title(port);
	while ( (cch=recv(sock, buf, 1500, 0)) > 0 ) {
		for ( char *p=buf; p<buf+cch; p++ ) {
			while ( *p==-1 && p<buf+cch ) {
				char *p0 = (char *)telnet_Options((unsigned char *)p);
				memcpy(p, p0, buf+cch-p0);
				cch -= p0-p;	//cch could become 0 after this
			}
		}
		buf[cch] = 0;
		if ( cch>0 ) term_Parse( buf, cch );
	}
	tiny_Title("");
	host_type = 0;
	closesocket(sock);

socket_close:
	host_status=CONN_IDLE;
	dwReaderId = 0;

	return 1;
}
#define TNO_IAC		0xff
#define TNO_DONT	0xfe
#define TNO_DO		0xfd
#define TNO_WONT	0xfc
#define TNO_WILL	0xfb
#define TNO_SUB		0xfa
#define TNO_ECHO	0x01
#define TNO_AHEAD	0x03
#define TNO_WNDSIZE 0x1f
#define TNO_TERMTYPE 0x18
#define TNO_NEWENV	0x27
UCHAR NEGOBEG[]={0xff, 0xfb, 0x03, 0xff, 0xfd, 0x03, 0xff, 0xfd, 0x01};
UCHAR TERMTYPE[]={0xff, 0xfa, 0x18, 0x00, 0x76, 0x74, 0x31, 0x30, 0x30, 0xff, 0xf0};
unsigned char * telnet_Options( unsigned char *buf )
{
	UCHAR negoreq[]={0xff,0,0,0, 0xff, 0xf0};
	unsigned char *p = buf+1;
		switch ( *p++ ) {
		case TNO_DO:
			if ( *p==TNO_TERMTYPE || *p==TNO_NEWENV || *p==TNO_ECHO ) {
				negoreq[1]=TNO_WILL; negoreq[2]=*p;
				term_Send((char*)negoreq, 3);
				if ( *p==TNO_ECHO ) bEcho = TRUE;
			}
			else { 						//if ( *p!=TNO_AHEAD ), 08/10 why?
				negoreq[1]=TNO_WONT; negoreq[2]=*p;
				term_Send((char*)negoreq, 3);
			}
			break;
		case TNO_SUB:
			if ( *p==TNO_TERMTYPE ) {
				term_Send((char*)TERMTYPE, sizeof(TERMTYPE));
			}
			if ( *p==TNO_NEWENV ) {
				negoreq[1]=TNO_SUB; negoreq[2]=*p;
				term_Send((char*)negoreq, 6);
			}
			p += 3;
			break;
		case TNO_WILL: 
			if ( *p==TNO_ECHO ) bEcho = FALSE;
			negoreq[1]=TNO_DO; negoreq[2]=*p;
			term_Send((char*)negoreq, 3);
			break;
		case TNO_WONT: 
			negoreq[1]=TNO_DONT; negoreq[2]=*p;
			term_Send((char*)negoreq, 3);
		   break;
		case TNO_DONT:
			break;
		}
	return p+1; 
}

/***************************STDIO*******************************/
static HANDLE Std_IN_Rd = NULL;
static HANDLE Std_IN_Wr = NULL;
static HANDLE Std_OUT_Rd = NULL;
static HANDLE Std_OUT_Wr = NULL;
static PROCESS_INFORMATION piStd; 
DWORD WINAPI stdio( void *pv)
{
	memset( &piStd, 0, sizeof(PROCESS_INFORMATION) );	
	//Set up PROCESS_INFORMATION 

	SECURITY_ATTRIBUTES saAttr; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE;			//Set the bInheritHandle flag
	saAttr.lpSecurityDescriptor = NULL;		//so pipe handles are inherited

	CreatePipe(&Std_OUT_Rd, &Std_OUT_Wr, &saAttr, 0);//pipe for child's STDOUT 
	SetHandleInformation(Std_OUT_Rd, HANDLE_FLAG_INHERIT, 0);
	// Ensure the read handle to the pipe for STDOUT is not inherited
	CreatePipe(&Std_IN_Rd, &Std_IN_Wr, &saAttr, 0);	//pipe for child's STDIN 
	SetHandleInformation(Std_IN_Wr, HANDLE_FLAG_INHERIT, 0);	
	// Ensure the write handle to the pipe for STDIN is not inherited

	struct _STARTUPINFOA siStartInfo;
	memset( &siStartInfo, 0, sizeof(STARTUPINFO) );	// Set STARTUPINFO
	siStartInfo.cb = sizeof(STARTUPINFO); 
	siStartInfo.hStdError = Std_OUT_Wr;
	siStartInfo.hStdOutput = Std_OUT_Wr;
	siStartInfo.hStdInput = Std_IN_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	if ( !CreateProcessA( NULL,			// Create the child process.
						(char *)pv,		// command line 
						NULL,			// process security attributes 
						NULL,			// primary thread security attributes 
						TRUE,			// handles are inherited 
						CREATE_NO_WINDOW,// creation flags 
						NULL,			// use parent's environment 
						NULL,			// use parent's current directory 
						&siStartInfo,	// STARTUPINFO pointer 
						&piStd) ) {		// receives PROCESS_INFORMATION 
		term_Disp("Couldn't create STDIO process\r\n");
		goto stdio_close;
	}
	CloseHandle( Std_IN_Rd );
	CloseHandle( Std_OUT_Wr );
	
	hWrite = Std_IN_Wr;
	host_status=CONN_CONNECTED;
	host_type = STDIO;
	tiny_Title( (char *)pv );
	DWORD dwCCH;
	char buf[1536];
	while ( ReadFile( Std_OUT_Rd, buf, 1500, &dwCCH, NULL) > 0 ) {
		buf[dwCCH] = 0;
		if ( dwCCH > 0 ) term_Parse( buf, dwCCH ); 
	}
	CloseHandle( Std_OUT_Rd );
	CloseHandle( Std_IN_Wr );
	tiny_Title("");
	host_type = 0;

stdio_close:
	host_status=CONN_IDLE;
	dwReaderId = 0;
	return 1;
}
void stdio_Close() 
{
	if ( WaitForSingleObject(piStd.hProcess, 100)==WAIT_TIMEOUT ) {
		term_Disp("Terminating stdio process...\r\n");
		TerminateProcess(piStd.hProcess,0);
	}
	CloseHandle(piStd.hThread);
	CloseHandle(piStd.hProcess);
}
/**********************************HTTPd*******************************/
const char HEADER[]="HTTP/1.1 200 Ok\
					\nServer: tinyTerm-httpd\
					\nAccess-Control-Allow-Origin: *\
					\nContent-Type: text/plain\
					\nContent-length: %d\
					\nConnection: Keep-Alive\
					\nCache-Control: no-cache\n\n";		//max-age=1
DWORD WINAPI *httpd( void *pv )
{
	char buf[1024], *cmd, *reply;
	struct sockaddr_in cltaddr;
	int addrsize=sizeof(cltaddr), cmdlen, replen;
	SOCKET http_s1, http_s0 = *(SOCKET *)pv;

	while ( (http_s1=accept(http_s0, (struct sockaddr*)&cltaddr, &addrsize))!= INVALID_SOCKET ) {
		cmd_Disp( "xmlhttp connected" );
		cmdlen=recv(http_s1,buf,1023,0);
		while ( cmdlen>5 ) {
			buf[cmdlen] = 0;
			if ( strncmp(buf, "GET /", 5)!=0 ) {//TCP connection
				FD_SET readset;
				struct timeval tv = { 0, 300 };	//tv_sec=0, tv_usec=300

				cmd_Disp(buf);
				replen = term_TL1( buf, &reply );
				send( http_s1, reply, replen, 0 );
				reply += replen;

				if ( host_status==CONN_CONNECTED ) do {
					replen = term_Recv( reply ); 
					if ( replen > 0 ) {
						send(http_s1, reply, replen, 0);
						reply += replen;
					}
					FD_ZERO(&readset);
					FD_SET(http_s1, &readset);
				} while ( select(1, &readset, NULL, NULL, &tv) == 0 );
			}
			else {											//HTTP connection
				if ( (cmd=strtok(buf, " ")) == NULL ) continue; 
				if ( (cmd=strtok(NULL, " ")) == NULL ) continue; 
				cmd+=2;
				char *p=cmd;
				for ( int i=0; cmd[i]!=0; i++ ) {
					cmd[i] = *p;
					if ( *p++=='%' ) {
						int a;
						sscanf( p, "%02x", &a);
						cmd[i] = a;
						p+=2;
					}
				}
				replen = 0;
				cmd_Disp(cmd+4);
				if ( strncmp(cmd,"Cmd=",4)==0 ) {
					if ( cmd[4]=='#' ) 
						reply = term_Exec( cmd+5 );
					else
						replen = term_TL1( cmd+4, &reply );
				} 
				else if ( strncmp(cmd,"Disp=",5)==0 ) {
					reply = term_Disp( cmd+5 );
				} 
				else if ( strncmp(cmd,"Send=",5)==0 ) {
					reply = term_Send( cmd+5, strlen(cmd+5) );
				} 
				else if ( strncmp(cmd,"Recv=",5)==0 ) { 
					replen = term_Recv( reply );
					if ( replen<0 ) replen=0;
				}
				int len = sprintf( buf, HEADER, replen );
				send( http_s1, buf, len, 0 );
				send( http_s1, reply, replen, 0 );
				reply += replen;
			}
			cmdlen = recv( http_s1, buf, 1023, 0 );
		}
		cmd_Disp( "xmlhttp disconnected" );
		closesocket(http_s1);
	}
	return 0;
}
int http_Svr( char *intf ) 
{
static SOCKET http_s0=INVALID_SOCKET;

	if ( http_s0 !=INVALID_SOCKET ) {
		closesocket( http_s0 );
		http_s0 = INVALID_SOCKET;
		return 0;
	}

	http_s0 = socket(AF_INET, SOCK_STREAM, 0);
	if ( http_s0==INVALID_SOCKET ) return 0;

	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=inet_addr( intf );
	int p;
	for ( p=2024; p<2040; p++ ) {
		svraddr.sin_port=htons(p);
		if ( bind(http_s0, (struct sockaddr*)&svraddr, addrsize)!=SOCKET_ERROR ) {
			if ( listen(http_s0, 1)!=SOCKET_ERROR){
				DWORD dwThreadId;
				CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)httpd, 
											&http_s0, 0, &dwThreadId);
				return p;
			}
		}
	}
	closesocket(http_s0);
	http_s0 = INVALID_SOCKET;
	return 0;
}