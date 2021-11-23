#include <u.h>
#include <libc.h>
#include <plumb.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>
#include <bio.h>

typedef struct Line Line;

struct Line {
	int t;
	char *s;
	char *f;
	int l;
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
Image *bg;
Image *fg;
Image *scrollbg;
int scrollsize;
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
	Rune  rn;
	char *s;

	bg = cols[l->t];
	draw(screen, r, bg, nil, ZP);
	p = Pt(r.min.x + Hpadding, r.min.y + (Dy(r)-font->height)/2);
	for(s = l->s; *s; s++){
		if(*s == '\t')
			p = string(screen, p, fg, ZP, font, "    ");
		else if((p.x+Hpadding+stringwidth(font, " ")+stringwidth(font, ellipsis)>=textr.max.x)){
			string(screen, p, fg, ZP, font, ellipsis);
			break;
		}else{
			s += chartorune(&rn, s) - 1;
			p = runestringn(screen, p, fg, ZP, font, &rn, 1);
		}
	}
}

void
redraw(void)
{
	Rectangle lr;
	int i, h, y;

	draw(screen, sr, bg, nil, ZP);
	draw(screen, scrollr, scrollbg, nil, ZP);
	if(lcount>0){
		h = ((double)nlines/lcount)*Dy(scrollr);
		y = ((double)offset/lcount)*Dy(scrollr);
		scrposr = Rect(scrollr.min.x, scrollr.min.y+y, scrollr.max.x-1, scrollr.min.y+y+h);
	}else
		scrposr = Rect(scrollr.min.x, scrollr.min.y, scrollr.max.x-1, scrollr.max.y);
	draw(screen, scrposr, bg, nil, ZP);
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

int
indexat(Point p)
{
	int n;

	if (!ptinrect(p, textr))
		return -1;
	n = (p.y - textr.min.y) / lineh;
	if ((n+offset) >= lcount)
		return -1;
	return n;
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
	scrollsize = mousescrollsize(nlines);
	redraw();
}

void
initcols(void)
{
	Rectangle cr;

	cr = Rect(0, 0, 1, 1);
	bg			= allocimage(display, cr, screen->chan, 1, 0xffffffff);
	fg			= allocimage(display, cr, screen->chan, 1, 0x000000ff);
	cols[Lfile] = allocimage(display, cr, screen->chan, 1, 0xefefefff);
	cols[Lsep]  = allocimage(display, cr, screen->chan, 1, 0xeaffffff);
	cols[Ladd]  = allocimage(display, cr, screen->chan, 1, 0xe6ffedff);
	cols[Ldel]  = allocimage(display, cr, screen->chan, 1, 0xffeef0ff);
	cols[Lnone] = bg;
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
parseline(char *f, int n, char *s)
{
	Line *l;

	l = malloc(sizeof *l);
	if(l==nil)
		sysfatal("malloc: %r");
	l->t = linetype(s);
	l->s = s;
	l->l = n;
	if(l->t != Lfile && l->t != Lsep)
		l->f = f;
	else
		l->f = nil;
	return l;
}

int
lineno(char *s)
{
	char *p, *t[5];
	int n, l;

	p = strdup(s);
	n = tokenize(p, t, 5);
	if(n<=0)
		return -1;
	l = atoi(t[2]);
	free(p);
	return l;
}

void
parse(int fd)
{
	Biobuf *bp;
	Line *l;
	char *s, *f, *t;
	int n, ab;

	ab = 0;
	n = 0;
	f = nil;
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
		l = parseline(f, n, s);
		if(l->t == Lfile && l->s[0] == '-' && strncmp(l->s+4, "a/", 2)==0)
			ab = 1;
		if(l->t == Lfile && l->s[0] == '+'){
			f = l->s+4;
			if(ab && strncmp(f, "b/", 2)==0)
				f += 1;
			t = strchr(f, '\t');
			if(t!=nil)
				*t = 0;
		}else if(l->t == Lsep)
			n = lineno(l->s);
		else if(l->t == Ladd || l->t == Lnone)
			++n;
		lines[lcount++] = l;
		if(lcount>=lsize){
			lsize *= 2;
			lines = realloc(lines, lsize*sizeof *lines);
			if(lines==nil)
				sysfatal("realloc: %r");
		}
	}
}

void
plumb(char *f, int l)
{
	USED(l);
	int fd;
	char wd[256], addr[300]={0};

	fd = plumbopen("send", OWRITE);
	if(fd<0)
		return;
	getwd(wd, sizeof wd);
	snprint(addr, sizeof addr, "%s:%d", f, l);
	plumbsendtext(fd, "vdiff", "edit", wd, addr);
	close(fd);
}

void
main(void)
{
	Event ev;
	int e, n;

	parse(0);
	if(lcount==0){
		fprint(2, "no diff\n");
		exits(nil);
	}
	if(initdraw(nil, nil, "vdiff")<0)
		sysfatal("initdraw: %r");
	initcols();
	einit(Emouse|Ekeyboard);
	eresized(0);
	for(;;){
		e = event(&ev);
		switch(e){
		case Emouse:
			if(ev.mouse.buttons&4 && ptinrect(ev.mouse.xy, scrollr)){
				n = (ev.mouse.xy.y - scrollr.min.y) / lineh;
				if(n<lcount-offset){
					scroll(n);
				} else {
					scroll(lcount-offset);
				}
				break;
			}
			if(ev.mouse.buttons&1 && ptinrect(ev.mouse.xy, scrollr)){
				n = (ev.mouse.xy.y - scrollr.min.y) / lineh;
				if(-n<lcount-offset){
					scroll(-n);
				} else {
					scroll(-lcount+offset);
				}
				break;
			}

			if(ev.mouse.buttons&4){
				n = indexat(ev.mouse.xy);
				if(n>=0 && lines[n+offset]->f != nil)
					plumb(lines[n+offset]->f, lines[n+offset]->l);
			}else if(ev.mouse.buttons&8)
				scroll(-scrollsize);
			else if(ev.mouse.buttons&16)
				scroll(scrollsize);
			break;
		case Ekeyboard:
			switch(ev.kbdc){
			case 'q':
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
