#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>
#include <bio.h>

typedef struct Line Line;

struct Line {
	int t;
	char *s;
};

enum
{
	Lfile = 0,
	Lsep,
	Ladd,
	Ldel,
	Lnone,
	Ncols,
};	

enum
{
	Scrollwidth = 12,
	Scrollgap = 2,
	Margin = 8,
	Hpadding = 4,
	Vpadding = 2,
};

Rectangle sr;
Rectangle scrollr;
Rectangle scrposr;
Rectangle listr;
Rectangle textr;
Image *cols[Ncols];
Image *scrollbg;
int lineh;
int nlines;
int offset;
Line **lines;
int lsize;
int lcount;
const char ellipsis[] = "...";

void
drawline(Rectangle r, Line *l)
{
	Image *bg;
	Point p;
	char *s;

	bg = cols[l->t];
	draw(screen, r, bg, nil, ZP);
	p = Pt(r.min.x + Hpadding, r.min.y + (Dy(r)-font->height)/2);
	for(s = l->s; *s; s++){
		if(*s == '\t')
			p = string(screen, p, display->black, ZP, font, "    ");
		else if((p.x+Hpadding+stringwidth(font, " ")+stringwidth(font, ellipsis)>=textr.max.x)){
			string(screen, p, display->black, ZP, font, ellipsis);
			break;
		}else
			p = stringn(screen, p, display->black, ZP, font, s, 1);
	}
}

void
redraw(void)
{
	Rectangle lr;
	int i, h, y;

	draw(screen, sr, display->white, nil, ZP);
	draw(screen, scrollr, scrollbg, nil, ZP);
	if(lcount>0){
		h = ((double)nlines/lcount)*Dy(scrollr);
		y = ((double)offset/lcount)*Dy(scrollr);
		scrposr = Rect(scrollr.min.x, scrollr.min.y+y, scrollr.max.x-1, scrollr.min.y+y+h);
	}else
		scrposr = Rect(scrollr.min.x, scrollr.min.y, scrollr.max.x-1, scrollr.max.y);
	draw(screen, scrposr, display->white, nil, ZP);
	for(i=0; i<nlines && offset+i<lcount; i++){
		lr = Rect(textr.min.x, textr.min.y+i*lineh, textr.max.x, textr.min.y+(i+1)*lineh);
		drawline(lr, lines[offset+i]);
	}
}

void
scroll(int off)
{
	if(off<0 && offset<=0)
		return;
	if(off>0 && offset+nlines>lcount)
		return;
	offset += off;
	if(offset<0)
		offset = 0;
	if(offset+nlines>lcount)
		offset = lcount-nlines+1;
	redraw();
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone)<0)
		sysfatal("cannot reattach: %r");
	sr = screen->r;
	scrollr = sr;
	scrollr.max.x = scrollr.min.x+Scrollwidth+Scrollgap;
	listr = sr;
	listr.min.x = scrollr.max.x;
	textr = insetrect(listr, Margin);
	lineh = Vpadding+font->height+Vpadding;
	nlines = Dy(textr)/lineh;
	offset = 0;
	redraw();
}

void
initcols(void)
{
	Rectangle cr;

	cr = Rect(0, 0, 1, 1);
	cols[Lfile] = allocimage(display, cr, screen->chan, 1, 0xefefefff);
	cols[Lsep]  = allocimage(display, cr, screen->chan, 1, 0xeaffffff);
	cols[Ladd]  = allocimage(display, cr, screen->chan, 1, 0xe6ffedff);
	cols[Ldel]  = allocimage(display, cr, screen->chan, 1, 0xffeef0ff);
	cols[Lnone] = display->white;
	scrollbg    = allocimage(display, cr, screen->chan, 1, 0x999999ff);
}

int
linetype(char *text)
{
	int type;

	type = Lnone;
	if(strncmp(text, "+++", 3)==0)
		type = Lfile;
	else if(strncmp(text, "---", 3)==0)
		type = Lfile;
	else if(strncmp(text, "@@", 2)==0)
		type = Lsep;
	else if(strncmp(text, "+", 1)==0)
		type = Ladd;
	else if(strncmp(text, "-", 1)==0)
		type = Ldel;
	return type;
}

Line*
parseline(char *s)
{
	Line *l;

	l = malloc(sizeof *l);
	if(l==nil)
		sysfatal("malloc: %r");
	l->t = linetype(s);
	l->s = s;
	return l;
}

void
parse(int fd)
{
	Biobuf *bp;
	char *s;

	lsize = 64;
	lcount = 0;
	lines = malloc(lsize * sizeof *lines);
	if(lines==nil)
		sysfatal("malloc: %r");
	bp = Bfdopen(fd, OREAD);
	if(bp==nil)
		sysfatal("Bfdopen: %r");
	for(;;){
		s = Brdstr(bp, '\n', 1);
		if(s==nil)
			break;
		lines[lcount++] = parseline(s);
		if(lcount>=lsize){
			lsize *= 2;
			lines = realloc(lines, lsize*sizeof *lines);
			if(lines==nil)
				sysfatal("realloc: %r");
		}
	}
}

void
main(void)
{
	Event ev;
	int e;

	parse(0);
	if(initdraw(nil, nil, "vdiff")<0)
		sysfatal("initdraw: %r");
	initcols();
	einit(Emouse|Ekeyboard);
	eresized(0);
	for(;;){
		e = event(&ev);
		switch(e){
		case Emouse:
			if(ev.mouse.buttons&8)
				scroll(-10);
			else if(ev.mouse.buttons&16)
				scroll(10);
			break;
		case Ekeyboard:
			switch(ev.kbdc){
			case Kdel:
				goto End;
				break;
			case Khome:
				scroll(-1000000);
				break;
			case Kend:
				scroll(1000000);
				break;
			case Kpgup:
				scroll(-nlines);
				break;
			case Kpgdown:
				scroll(nlines);
				break;
			}
			break;
		}
	}
End:
	exits(nil);
}
