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

void color(uint8_t *p) {
	if(p>=texts && p<=textd) { printf("%s",CSI"7m"); }
	else { printf("%s",CSI"m"); }
}

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
			if(*p=='\n') { color(p); putchar(' '); break; }

			color(p);
			if(*p=='\t') {
				int t=c%8; if(!t)t=8;
				while(t--) putchar(' '); p++;
			} else {
				putchar(*p++);
			}
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

uint8_t **atext;

uint8_t *linesbackward(uint8_t *p, int n) {
	n++;
	while(p>text) {
		if(*p=='\n') n--;
		if(n==0) { return p+1; }
		p--;
	}
}

uint8_t *linesforward(uint8_t *p, int n) {
	while(p<texte) {
		if(*p++=='\n') { n--;}
		if(n==0) { return p; }
	}
}

int dir=0;

void number() {
	int n=0;
	for(;;) {
		switch(*input) {
		case '0'...'9': n*=10;n+=(*input++)-'0'; break;
		default:
			switch(dir) {
			case 0: *atext=linesforward(text,n); break;
			case -1: *atext=linesbackward(*atext,n); break;
			case 1: *atext=linesforward(*atext,n); break;
			}
			return;
		}
	}
}

void cmd() {
	switch(*input) {
	case 'q': resetterm(); exit(0); break;
	case 0: break;
	default:input++;
	}
}

void interpret() {
	*input=0;
	input=inputbuf;

	atext=&texts;
	dir=0;

	for(;;) {
		switch(*input) {
		case 0:
			textd=texts;
			goto end;
		case '0'...'9': {
			number();
		} break;
		case '+':dir=1; input++; break;
		case '-':dir=-1; input++; break;
		case 'a'...'z':
			cmd();
			break;
		default:;
		}
	}
end:
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
		pos(win.ws_row-1,1);
		printf("%lu(%02x):%lu",texts-text,*texts,textd-text); fflush(stdout);

		pos(win.ws_row,1);
		printf("%.*s",(int)(input-inputbuf),inputbuf); fflush(stdout);

		int r=read(0,&c,1);
		if(c=='\r') { interpret(); continue; }
		*input++=c;
	}


	resetterm();
}

