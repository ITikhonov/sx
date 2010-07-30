#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>


struct termios oldkey;

void initterm() {
        struct termios newkey;
        tcgetattr(STDIN_FILENO,&oldkey);
        newkey.c_cflag = B9600 | CRTSCTS | CS8 | CLOCAL | CREAD;
        newkey.c_iflag = IGNPAR;
        newkey.c_oflag = 0;
        newkey.c_lflag = 0;
        newkey.c_cc[VMIN]=1;
        newkey.c_cc[VTIME]=0;
        tcflush(STDIN_FILENO, TCIFLUSH);
        tcsetattr(STDIN_FILENO,TCSANOW,&newkey);
}

void resetterm() {
	tcsetattr(STDIN_FILENO,TCSANOW,&oldkey);
}

uint8_t *text;
uint8_t *textw;
uint8_t *texte;

uint8_t *texts;
uint8_t *textd;

void openfile() {
	int fd=open("sample.txt",O_RDWR);
	struct stat st;
	fstat(fd,&st);
	texts=textd=textw=text=mmap(0,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,0);
	texte=text+st.st_size;
}

struct winsize win;

#define CSI "\x1b["

void pos(int x,int y) {
	char buf[15];
	printf(CSI "%u;%uf",x,y); fflush(stdout);
}

void eraseline() { printf(CSI "K"); fflush(stdout); }
void clear() { printf(CSI "J"); fflush(stdout); }

void drawtext() {
	pos(1,1);
	int r=0;
	uint8_t *p=textw;
	for(;r<win.ws_row-2;r++) {
		eraseline();
		int c=0;
		for(;p<texte;) {
			if(*p=='\t') {c+=8;} else {c++;}
			if(c>=win.ws_col) break;
			if(*p=='\n') break;
			if(p>=texts && p<=textd) {
				printf("%s",CSI"7m");
			} else {
				printf("%s",CSI"m");
			}
			putchar(*p++);
		}
		while(*p!='\n') {
			p++;
			if(p==texte) goto end;
		}
		p++;
		putchar('\r');
		putchar('\n');
		if(p==texte) goto end;
	}

end:	
	clear();
	fflush(stdout);
}

char inputbuf[1024];
char *input=inputbuf;

void toline(int n) {
	uint8_t *p=text;
	while(p<texte) {
		if(*p=='\n') n--;
		if(n==0) { textd=p; p--; break; }
		p++;
	}

	texts=0;

	n=win.ws_row/2;
	while(p>text) {
		if(*p=='\n') {if(!texts)texts=p; n--;}
		if(n==0) break;
		p--;
	}

	textw=p;
}

void number() {
	int n=0;
	for(;;) {
		switch(*input) {
		case '0'...'9': n*=10;n+=(*input++)-'0'; break;
		default:
			toline(n);
			return;
		}
	}
}

void interpret() {
	*input=0;
	input=inputbuf;

	switch(*input) {
	case 0: break;
	case '0'...'9': {
		number();
	} break;
	case 'q': resetterm(); exit(0);
	default:;
	}

	input=inputbuf;
}


int main(int argc, char *argv[]) {
	ioctl(1,TIOCGWINSZ,&win);

	openfile(argv[1]);
	initterm();

	pos(1,1); clear();

	char c;
	for(;;) {
		drawtext();
		pos(win.ws_row,1);
		printf("%.*s",(int)(input-inputbuf),inputbuf); fflush(stdout);

		int r=read(0,&c,1);
		if(c=='\r') { interpret(); continue; }
		*input++=c;
	}


	resetterm();
}

