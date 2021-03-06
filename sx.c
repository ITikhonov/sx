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
#include <errno.h>

#define __USE_GNU

#include <regex.h>


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

int filefd;

char *name;
char swapname[256];
int changed=0;

void makeswap(char *f) {
	char *sp=rindex(f,'/');
	if(sp) {
		sp[0]=0;
		chdir(f);
		name=sp+1;
	} else {
		name=f;
	}

	sprintf(swapname,".sx.%.251s",name);

	int swap=open(swapname,O_WRONLY|O_CREAT,0644);
	int orig=open(name,O_RDONLY);
	char buf[1024];
	int r;
	while((r=read(orig,buf,1024))>0) {
		write(swap,buf,r);
	}
	close(orig);
	close(swap);
}

uint8_t empty;

void openfile() {
	filefd=open(swapname,O_RDWR);
	struct stat st;
	if(fstat(filefd,&st)==-1 || st.st_size==0) { textd=textw=text=texts=texte=&empty; return; }
	texts=textd=textw=text=mmap(0,st.st_size,PROT_READ|PROT_WRITE,MAP_PRIVATE,filefd,0);
	texte=text+st.st_size;
}

struct winsize win;

#define CSI "\x1b["

void pos(int x,int y) {
	printf(CSI "%u;%uf",x,y); fflush(stdout);
}

void eraseline() { printf(CSI "K"); fflush(stdout); }
void clear() { printf(CSI "J"); fflush(stdout); }

uint8_t color(uint8_t *p) {
	if(p>=texts && p<textd) { return 7; }
	else { return 0xff; }
}

void setcolor(uint8_t c) {
	printf("%s",CSI"m");
	if(c!=0xff) printf(CSI"%hhum",c);
}

uint8_t *rewindline(uint8_t *p) {
	for(;p>=text && *p!='\n';) p--;
	return p+1;
}

int physlines(int n) {
	if(n<win.ws_col) return 1;
	if(n<2*win.ws_col-3) return 2;
	return (n-(win.ws_col-3))/(win.ws_col-6)+2;
}


int draw_col, draw_row;


uint8_t *rewindview() {
	uint8_t *p=texts;
	int n=0;
	int br=0;

	for(;;) {
		uint8_t *s=rewindline(p);
		int l=physlines(p-s);
		n+=l;

		if(n>win.ws_row/2) {
			int m=n-win.ws_row/2;
			p=s+win.ws_col-3 + (win.ws_col-6)*(m-1);
			br=1;
			break;
		}

		if(n==win.ws_row/2) { p=s; break; }
		if(s==text) { p=text; break; }
		p=s-2;
	}
	if(br) { printf("%s...",CSI"1m"); }
	return p;
}

int drawchar(uint8_t c, uint8_t cl) {
	if(c=='\n') {
		draw_row++; 
		if(draw_row==win.ws_row-2) return 1;
		draw_col=0; printf("\r\n"); eraseline();
		return 0;
	}

	if(draw_col>=win.ws_col-3) {
		printf("%s...",CSI"1m");
		printf("\r\n"); eraseline();
		printf("%s...",CSI"1m");
		draw_row++;
		draw_col=3;;
	}
	setcolor(cl);
	putchar(c);
	draw_col++;
	return 0;
}

uint8_t inputbuf[1024];
uint8_t *input=inputbuf;

void drawtext2(uint8_t *p, uint8_t *e) {
	for(;p<e;) {
		uint8_t cl=color(p);
		if(p==texts) { drawchar('>',1); }
		if(p==textd) {
			if(input>inputbuf && (inputbuf[0]=='a' || inputbuf[0]=='A')) {
				if(inputbuf[0]=='A') drawchar('\n',cl);
				drawtext2(inputbuf+1,input);
			}
			drawchar('<',1);
		}
		if(*p=='\t') {
			p++;
			drawchar(' ',cl); drawchar(' ',cl); drawchar(' ',cl); drawchar(' ',cl);
			drawchar(' ',cl); drawchar(' ',cl); drawchar(' ',cl); drawchar(' ',cl);
			continue;
		}
		if(drawchar(*p++, cl)) break;
		
	}
}

void drawtext() {
	pos(1,1); draw_col=draw_row=0; eraseline();
	uint8_t *p=rewindview();

	drawtext2(p,texte);

	clear();
	fflush(stdout);
}

int dir=0;
int bindend=0;
const char *err="";
char errbuf[1024];

struct range { uint8_t *s,*e; };

struct range atext;
struct re_pattern_buffer regp;

struct range regexsearch(uint8_t *s, uint8_t *e, uint8_t *p, uint8_t *pe) {
	struct re_pattern_buffer reg;
	if(pe-p) {
		memset(&reg,0,sizeof(reg));
		const char *errl;
		if((errl=re_compile_pattern((char*)p,pe-p, &reg))) {
			err=errl;
			struct range ret={s,s};
			return ret;
		}
		regp=reg;
	} else {
		reg=regp;
	}

	struct re_registers mreg;
	int r=re_search(&reg, (char*)text,texte-text, s-text,e-s, &mreg);
	if(r==-1) {
		sprintf(errbuf,"pat /%.*s/ not found (%d,%d,%d)",(int)(pe-p),p,(int)(s-text),(int)(e-text),dir);
		err=errbuf;
		struct range ret={s,s};
		return ret;
	}

	struct range ret={text+mreg.start[0],text+mreg.end[0]};
	sprintf(errbuf,"pat /%.*s/ %d:%d n=%d r=%d",(int)(pe-p),p,mreg.start[0],mreg.end[0],mreg.num_regs,r);
	err=errbuf;
	return ret;
}

void regex() {
	input++;
	uint8_t *pat=input, *e=input;
	int esc=0;
	
	for(;;) {
		switch(*e) {
		case 0: input=e; goto go;
		case '/': if(!esc) { input=e+1; goto go; } esc=0; break;
		case '\\': esc=1; break;
		default: esc=0;
		}
		e++;
	}

go:;
	struct range r;
	if(dir>=0) {
		r=regexsearch(atext.e,texte,pat,e);
	} else {
		r=regexsearch(atext.s-1,text,pat,e);
	}
	atext.s=r.s;
	atext.e=r.e;
}

uint8_t *linesbackward(uint8_t *p, int n) {
	n++;
	while(p>text) {
		if(*p=='\n') n--;
		if(n==0) { return p+1; }
		p--;
	}
	return text;
}

uint8_t *linesforward(uint8_t *p, int n) {
	if(n==0) return p;
	while(p<texte) {
		if(*p++=='\n') { n--;}
		if(n==0) { break; }
	}
	return p;
}


void number() {
	int n=0;
	for(;;) {
		switch(*input) {
		case '0'...'9': n*=10;n+=(*input++)-'0'; break;
		default:
			switch(dir) {
			case 0: if(n==0) {atext.s=atext.e=text; break;}
				atext.s=linesforward(text,n-1); atext.e=linesforward(atext.s,1);  break;
			case -1: atext.s=linesbackward(atext.s,n); atext.e=linesforward(atext.s,1); break;
			case 1: atext.s=linesforward(atext.s,n); atext.e=linesforward(atext.s,1); break;
			}
			return;
		}
	}
}

void cnumber() {
	int n=0;
	input++;
	uint8_t **a=bindend?&atext.e:&atext.s;
	for(;;) {
		switch(*input) {
		case '0'...'9': n*=10;n+=(*input++)-'0'; break;
		default:
			switch(dir) {
			case 0: *a=text+n; break;
			case -1: *a-=n; break;
			case 1: *a+=n; break;
			}
			return;
		}
	}
}

int precopy(int at) {
	munmap(text,texte-text);

	char buf[1024];
	swapname[1]='~';
	int fd=open(swapname,O_WRONLY|O_CREAT,0644);
	swapname[1]='s';
	int off=0;
	
	for(;off+1024<at;) {
		int r=read(filefd,buf,1024);
		if(r<=0) break;
		write(fd,buf,r);
		off+=r;
	}
	int r=read(filefd,buf,at-off);
	write(fd,buf,r);
	return fd;
}

void postcopy(int fd, int at) {
	changed=1;
	lseek(filefd,at,SEEK_SET);
	char buf[1024];
	for(;;) {
		int r=read(filefd,buf,1024);
		if(r<=0) break;
		write(fd,buf,r);
	}
	close(fd);

	char swapname2[256];
	strcpy(swapname2,swapname);
	swapname2[1]='~';

	rename(swapname2,swapname);
	uint8_t *otext=text;
	uint8_t *otextw=textw;
	uint8_t *otexts=texts;
	uint8_t *otextd=textd;
	openfile();
	textw=text+(otextw-otext);
	texts=text+(otexts-otext);
	textd=text+(otextd-otext);
}

void delete() {
	input++;
	swapname[1]='y';
	int y=open(swapname,O_CREAT|O_TRUNC|O_WRONLY,0600);
	swapname[1]='s';
	uint8_t *p=texts;
	while(p+1024<textd) {
		write(y,p,1024); p+=1024;
	}

	write(y,p,textd-p);
	close(y);

	int fd=precopy(texts-text);
	postcopy(fd,textd-text);
	textd=texts;
}


void paste() {
	input++;
	int fd=precopy(textd-text);

	swapname[1]='y';
	int y=open(swapname,O_RDONLY);
	swapname[1]='s';
	int t=0;
	for(;;) {
		char buf[1024];
		int r=read(y,buf,1024);
		if(r<=0) break;
		t+=r;
		write(fd,buf,r);
	}
	postcopy(fd,textd-text);
	texts=textd;
	textd=texts+t;
}

void append() {
	input++;
	int len=strlen((char*)input);

	int fd=precopy(textd-text);
	write(fd,input,len);
	postcopy(fd,textd-text);

	texts=textd;
	textd+=len;
	input+=len;
}

void nlappend() {
	*input--='\n';
	append();
}

void save() {
	input++;
	
	char swapname2[256];
	strcpy(swapname2,swapname);
	swapname2[1]='~';

	int swap=open(swapname,O_RDONLY,0644);
	int orig=open(swapname2,O_WRONLY|O_CREAT);
	char buf[1024];
	int r;
	while((r=read(swap,buf,1024))>0) {
		write(orig,buf,r);
	}
	close(orig);
	close(swap);
	rename(swapname2,name);
	changed=0;
}


void quit() {
	if(!changed) {
		resetterm();
		unlink(swapname);
		exit(0);
	}

	err="not saved, use Q or save first";
	input++;
}

int show;

void cmd() {
	if(!bindend) texts=atext.s;
	textd=atext.e;

	if(textd<texts) { uint8_t *a=texts; texts=textd; textd=a; }

	switch(*input) {
	case 'q': quit(); break;
	case 'Q': changed=0; quit(); break;
	case '=': show=0; input++; break;
	case 'a': append(); break;
	case 'A': nlappend(); break;
	case 'd': delete(); break;
	case 'w': save(); break;
	case 0: break;
	case 'p': paste(); break;
	default:input++;
	}
}

void interpret() {
	show=1;
	*input=0;
	input=inputbuf;
	err="";

	if(input[0]=='\0') {
		inputbuf[0]='A';
		input=inputbuf+1;
		return;
	}

	atext.s=texts;
	atext.e=textd;

	dir=0;
	bindend=0;

	for(;;) {
		switch(*input) {
		case '$':atext.s=atext.e=texte; input++; break;
		case '0'...'9': number(); break;
		case '#': cnumber(); break;
		case '+':dir=1; input++; break;
		case '-':dir=-1; input++; break;
		case '.': atext.e=atext.s; input++; break;
		case ',':
			texts=atext.s;
			dir=0;
			bindend=1;
			input++;
			break;
		case '/': regex(); break;
		case 0: case '=': case 'a'...'z': case 'A': case 'Q':
			cmd();
			atext.s=texts; atext.e=textd;
			if(!*input) goto end;
			break;
		default:
			err="! unknown command";
			goto end;
		}
	}
end:
	if(show) { textw=linesbackward(texts,win.ws_row/2); }
	input=inputbuf;
}

int main(int argc, char *argv[]) {
	ioctl(1,TIOCGWINSZ,&win);

	makeswap(argv[1]);
	openfile();
	initterm();

	pos(1,1); clear();

	char c;
	for(;;) {
		drawtext();
		pos(win.ws_row-1,1);
		eraseline(); printf("%lu(%02x):%lu %s",texts-text,*texts,textd-text,err); fflush(stdout);

		pos(win.ws_row,1);
		eraseline(); printf("%.*s",(int)(input-inputbuf),inputbuf); fflush(stdout);

		int r=read(0,&c,1);
		if(r==-1) {
			if(errno==EAGAIN || errno==EINTR) continue;
			break;
		}
		if(r==0) break;

		if(c=='\x7f') { if(input>inputbuf) input--; continue; }
		if(c=='\r') { interpret(); continue; }

		*input++=c;
	}


	resetterm();
	return 0;
}

