;	WORMS: worms wander about the screen, after BSD worms(6), in colour.
;
;	WORMS [-D ms] [-F] [-L length] [-N number] [-T] [-M]
;
;	-D ms	  delay between steps (default 40; 0: as fast as it goes)
;	-F	  fill the screen with a field of dots for the worms to eat
;	-L length segments per worm, 2-255 (default 16)
;	-N number worms, 1-40 (default 3)
;	-T	  worms leave a trail of dots
;	-M	  monochrome: no colour codes
;
;	Any key ends it. Written for the terminal's VT100 emulation: the
;	screen size comes from a cursor position report (24 x 80 if none
;	arrives), each worm is drawn in its own bright colour (SGR 91-96)
;	with its own glyph. The delay loop is timed against the clock
;	(ports 65/66) at the start, so -D is in milliseconds at any speed.
;
;	Like BSD worms, every worm starts at the bottom left corner and on
;	each step turns 45 degrees left, goes on or turns 45 degrees right
;	at random, keeping to the screen; a cell is cleared when the last
;	worm on it leaves. This is a new program in Z80 assembly following
;	the behaviour of BSD worms (Eric S. Raymond and others, BSD
;	licence), not a translation of its code.
;
;	Assembled with z80pack's z80asm by build.sh, put on the CPM22 disk
;	by diskimg_gen.py.

BDOS	EQU	5
BDOSPG	EQU	7		;high byte of the BDOS entry: top of the TPA
TAIL	EQU	80H		;command tail: length, then the text
CLKCMD	EQU	65		;clock: register select
CLKDAT	EQU	66		;clock: register data (0: seconds)
ESC	EQU	1BH
MAXW	EQU	40
NONE	EQU	0FFH		;x of a segment not on the screen yet

	ORG	100H

START:	LD	SP,STACK
	CALL	ARGS
	JP	C,USAGE
	CALL	SCRSIZE
	CALL	SETUP
	JP	C,NOROOM
	CALL	CALIB
	CALL	SEED
MAIN:	CALL	STEP
	LD	E,0FFH		;a key ends it
	LD	C,6
	CALL	BDOS
	OR	A
	JR	NZ,QUIT
	CALL	DELAY
	JR	MAIN

QUIT:	LD	HL,SBYE
	CALL	PUTS
	JP	0

USAGE:	LD	DE,SUSAGE
	LD	C,9
	CALL	BDOS
	JP	0

NOROOM:	LD	DE,SROOM
	LD	C,9
	CALL	BDOS
	JP	0

;	---- options ----

;	Read the options from the command tail; carry set if they are bad.
ARGS:	LD	HL,TAIL
	LD	B,(HL)		;B: characters left
	INC	HL
ARG1:	CALL	SKIPSP
	RET	Z		;the end (carry clear)
	LD	A,(HL)
	CP	'-'
	SCF
	RET	NZ
	CALL	NEXT
	RET	C
	CP	'F'
	JR	Z,ARGF
	CP	'T'
	JR	Z,ARGT
	CP	'M'
	JR	Z,ARGM
	CP	'D'
	JR	Z,ARGD
	CP	'L'
	JR	Z,ARGL
	CP	'N'
	JR	Z,ARGN
	SCF
	RET
ARGF:	LD	A,1
	LD	(FIELD),A
	JR	ARG1
ARGT:	LD	A,'.'
	LD	(TRAIL),A
	JR	ARG1
ARGM:	LD	A,1
	LD	(MONO),A
	JR	ARG1
ARGD:	CALL	NUMBER
	RET	C
	LD	(DELMS),DE
	JR	ARG1
ARGL:	CALL	NUMBER
	RET	C
	LD	A,D		;2-255
	OR	A
	SCF
	RET	NZ
	LD	A,E
	CP	2
	RET	C
	LD	(LENGTH),A
	JR	ARG1
ARGN:	CALL	NUMBER
	RET	C
	LD	A,D		;1-MAXW
	OR	A
	SCF
	RET	NZ
	LD	A,E
	OR	A
	SCF
	RET	Z
	CP	MAXW+1
	CCF
	RET	C
	LD	(NWORMS),A
	JR	ARG1

;	Skip blanks; Z set at the end of the tail.
SKIPSP:	LD	A,B
	OR	A
	RET	Z
	LD	A,(HL)
	CP	' '
	JR	Z,SKIP1
	CP	9
	RET	NZ
SKIP1:	INC	HL
	DEC	B
	JR	SKIPSP

;	The next character of the tail in A (upper case), carry at the end.
NEXT:	INC	HL
	DEC	B
	LD	A,B
	OR	A
	SCF
	RET	Z
	LD	A,(HL)
	INC	HL
	DEC	B
	CP	'a'
	JR	C,NEXT1
	CP	'z'+1
	JR	NC,NEXT1
	SUB	20H
NEXT1:	OR	A		;carry clear
	RET

;	A decimal number (after blanks) in DE; carry if there is none or it
;	is over 65535.
NUMBER:	CALL	SKIPSP
	SCF
	RET	Z
	LD	DE,0
	LD	C,0		;digits seen
NUM1:	LD	A,B
	OR	A
	JR	Z,NUM2
	LD	A,(HL)
	SUB	'0'
	JR	C,NUM2
	CP	10
	JR	NC,NUM2
	PUSH	HL
	LD	H,D		;DE = DE * 10 + A
	LD	L,E
	ADD	HL,HL
	JR	C,NUMBIG
	ADD	HL,HL
	JR	C,NUMBIG
	ADD	HL,DE
	JR	C,NUMBIG
	ADD	HL,HL
	JR	C,NUMBIG
	LD	E,A
	LD	D,0
	ADD	HL,DE
	JR	C,NUMBIG
	EX	DE,HL
	POP	HL
	INC	HL
	DEC	B
	INC	C
	JR	NUM1
NUMBIG:	POP	HL
	SCF
	RET
NUM2:	LD	A,C
	OR	A
	SCF
	RET	Z
	CCF			;digits: carry clear
	RET

;	---- the screen size ----

;	Ask the terminal where the cursor ends up after moving it far down
;	and right: ESC [ rows ; cols R. Keeps 24 x 80 without an answer.
SCRSIZE:
	LD	HL,SASK
	CALL	PUTS
	CALL	GETC
	RET	C
	CP	ESC
	RET	NZ
	CALL	GETC
	RET	C
	CP	'['
	RET	NZ
	CALL	GETNUM
	RET	C
	CP	';'
	RET	NZ
	LD	A,E
	LD	(ROWS1),A
	CALL	GETNUM
	RET	C
	CP	'R'
	RET	NZ
	LD	A,(ROWS1)	;at least 3 x 3
	CP	3
	RET	C
	LD	B,A
	LD	A,E
	CP	3
	RET	C
	LD	(COLS),A
	LD	A,B
	LD	(ROWS),A
	RET

;	Digits from the console into E (up to 254), then the next character
;	in A; carry on a timeout or a bigger number.
GETNUM:	LD	E,0
GETN1:	CALL	GETC
	RET	C
	CP	'0'
	JR	C,GETN2
	CP	'9'+1
	JR	NC,GETN2
	SUB	'0'
	LD	D,A
	LD	A,E		;E = E * 10 + D
	CP	25
	CCF
	RET	C
	ADD	A,A
	LD	E,A
	ADD	A,A
	ADD	A,A
	ADD	A,E
	ADD	A,D
	RET	C
	CP	255
	CCF
	RET	C
	LD	E,A
	JR	GETN1
GETN2:	OR	A
	RET

;	A character from the console in A, carry if none comes for a while;
;	keeps DE.
GETC:	PUSH	DE
	LD	BC,2000
GETC1:	PUSH	BC
	LD	E,0FFH
	LD	C,6
	CALL	BDOS
	POP	BC
	OR	A
	JR	NZ,GETC2
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,GETC1
	POP	DE
	SCF
	RET
GETC2:	POP	DE
	OR	A		;carry clear
	RET

;	---- setting up ----

;	Lay out the cell counts and the worms' segments above the program,
;	clear the screen. Carry if they do not fit below the BDOS.
SETUP:	LD	HL,FREE		;row table: the start of each row's counts
	LD	(REFS),HL
	LD	DE,ROWTAB
	LD	A,(ROWS)
	LD	B,A
	LD	A,(COLS)
	LD	C,A
SETR:	EX	DE,HL		;ROWTAB[y] = HL
	LD	(HL),E
	INC	HL
	LD	(HL),D
	INC	HL
	EX	DE,HL
	PUSH	BC
	LD	B,0
	ADD	HL,BC
	POP	BC
	DJNZ	SETR
	LD	(SEGS),HL	;then the segments: NWORMS x LENGTH x (x, y)
	LD	A,(LENGTH)
	LD	E,A
	LD	D,0
	EX	DE,HL
	ADD	HL,HL
	EX	DE,HL		;DE: bytes per worm
	LD	A,(NWORMS)
	LD	B,A
	LD	IX,WSEGS
SETW:	LD	(IX+0),L
	LD	(IX+1),H
	INC	IX
	INC	IX
	ADD	HL,DE
	JR	C,SETBIG
	DJNZ	SETW
	LD	(TOP),HL
	LD	DE,64		;room under the BDOS
	ADD	HL,DE
	JR	C,SETBIG
	LD	A,(BDOSPG)
	CP	H
	JR	C,SETBIG
	JR	Z,SETBIG
	LD	HL,(REFS)	;no worm anywhere, every segment off screen
	LD	DE,(SEGS)
SETZ:	LD	(HL),0
	INC	HL
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	JR	NZ,SETZ
	LD	DE,(TOP)
SETF:	LD	(HL),NONE
	INC	HL
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	JR	NZ,SETF
	LD	HL,WORIENT	;heading up and right, head at segment 0
	LD	B,MAXW*2
SETO:	LD	(HL),0
	INC	HL
	DJNZ	SETO
	LD	HL,SINIT
	CALL	PUTS
	LD	A,(FIELD)
	OR	A
	CALL	NZ,DRFIELD
	OR	A
	RET
SETBIG:	SCF
	RET

;	Fill the screen with dots.
DRFIELD:
	LD	A,(ROWS)
	LD	B,A
	LD	C,0		;row
DRF1:	PUSH	BC
	LD	B,C
	LD	C,0
	CALL	GOTO
	LD	A,(COLS)
	LD	B,A
DRF2:	LD	A,'.'
	CALL	PUTC
	DJNZ	DRF2
	POP	BC
	INC	C
	DJNZ	DRF1
	RET

;	---- timing ----

;	Count delay units between two ticks of the seconds clock, then
;	UNITMS = units per millisecond (at least 1). Not with -D 0.
CALIB:	LD	HL,(DELMS)
	LD	A,H
	OR	L
	RET	Z
	XOR	A
	OUT	(CLKCMD),A
	IN	A,(CLKDAT)
	LD	C,A
CAL1:	IN	A,(CLKDAT)	;wait for the next second
	CP	C
	JR	Z,CAL1
	LD	C,A
	LD	HL,0
CAL2:	CALL	UNIT
	INC	HL
	LD	A,H
	OR	L
	JR	NZ,CAL3
	DEC	HL		;65535 at most
CAL3:	IN	A,(CLKDAT)
	CP	C
	JR	Z,CAL2
	LD	DE,1000		;units per ms: HL / 1000
	LD	BC,0
CAL4:	OR	A
	SBC	HL,DE
	JR	C,CAL5
	INC	BC
	JR	CAL4
CAL5:	LD	A,B
	OR	C
	JR	NZ,CAL6
	INC	C
CAL6:	LD	(UNITMS),BC
	RET

;	A fixed stretch of time: 256 turns of DJNZ.
UNIT:	LD	B,0
UNIT1:	DJNZ	UNIT1
	RET

;	Wait DELMS milliseconds.
DELAY:	LD	HL,(DELMS)
DEL1:	LD	A,H
	OR	L
	RET	Z
	LD	DE,(UNITMS)
DEL2:	CALL	UNIT
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,DEL2
	DEC	HL
	JR	DEL1

;	---- random numbers ----

;	Seed from the refresh register and the clock (never 0).
SEED:	LD	A,R
	LD	L,A
	XOR	A
	OUT	(CLKCMD),A
	IN	A,(CLKDAT)
	LD	H,A
	LD	A,1		;minutes too
	OUT	(CLKCMD),A
	IN	A,(CLKDAT)
	XOR	L
	LD	L,A
	OR	H
	JR	NZ,SEED1
	INC	L
SEED1:	LD	(RSEED),HL
	RET

;	A 16-bit xorshift (John Metcalf's), in HL and A = L.
RAND:	LD	HL,(RSEED)
	LD	A,H
	RRA
	LD	A,L
	RRA
	XOR	H
	LD	H,A
	LD	A,L
	RRA
	LD	A,H
	RRA
	XOR	L
	LD	L,A
	XOR	H
	LD	H,A
	LD	(RSEED),HL
	LD	A,L
	RET

;	---- the worms ----

;	Move every worm one step.
STEP:	XOR	A
STEP1:	LD	(WORM),A
	CALL	MOVE
	LD	A,(NWORMS)
	LD	B,A
	LD	A,(WORM)
	INC	A
	CP	B
	JR	C,STEP1
	RET

;	Move worm WORM: clear its tail cell if no other worm is on it, then
;	choose where its head goes and draw it there.
MOVE:	LD	A,(WORM)
	LD	E,A
	LD	D,0
	LD	HL,WHEAD
	ADD	HL,DE
	LD	C,(HL)		;C: head segment
	CALL	SEGADR		;HL: its (x, y)
	LD	A,(HL)
	CP	NONE
	JR	NZ,MOVE1
	LD	(HL),0		;first step: appear at the bottom left
	INC	HL
	LD	A,(ROWS)
	DEC	A
	LD	(HL),A
	DEC	HL
	PUSH	HL
	LD	C,0
	LD	B,A
	CALL	REFINC
	CALL	DRWORM
	POP	HL
MOVE1:	LD	A,(HL)		;the head's x, y
	LD	(HX),A
	INC	HL
	LD	A,(HL)
	LD	(HY),A
	LD	A,(WORM)	;on to the next segment, which is the tail
	LD	E,A
	LD	D,0
	LD	HL,WHEAD
	ADD	HL,DE
	LD	A,(HL)
	INC	A
	LD	B,A
	LD	A,(LENGTH)
	CP	B
	JR	NZ,MOVE2
	LD	B,0
MOVE2:	LD	(HL),B
	LD	C,B
	CALL	SEGADR
	PUSH	HL		;where the new head goes
	LD	A,(HL)
	CP	NONE
	JR	Z,MOVE3
	LD	C,A		;the tail: one worm fewer on its cell
	INC	HL
	LD	B,(HL)
	CALL	REFDEC
	JR	NZ,MOVE3
	CALL	GOTO
	XOR	A
	CALL	COLOUR
	LD	A,(TRAIL)
	CALL	PUTC
MOVE3:	CALL	TURN		;B, C: the new head's y, x
	POP	HL
	LD	(HL),C
	INC	HL
	LD	(HL),B
	CALL	REFINC
	JP	DRWORM

;	HL = the address of segment C of worm WORM.
SEGADR:	LD	A,(WORM)
	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,WSEGS
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	LD	B,0
	ADD	HL,BC
	ADD	HL,BC
	RET

;	HL = the count of worms on cell (column C, row B).
REFADR:	PUSH	DE
	LD	L,B
	LD	H,0
	ADD	HL,HL
	LD	DE,ROWTAB
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	LD	E,C
	LD	D,0
	ADD	HL,DE
	POP	DE
	RET

REFINC:	CALL	REFADR
	INC	(HL)
	RET

;	One worm fewer on cell (C, B); Z set if none is left.
REFDEC:	CALL	REFADR
	DEC	(HL)
	RET

;	Draw worm WORM's glyph in its colour at (column C, row B).
DRWORM:	CALL	GOTO
	LD	A,(WORM)
DRW1:	SUB	6		;colour: SGR 91-96 by WORM mod 6
	JR	NC,DRW1
	ADD	A,6+91
	CALL	COLOUR
	LD	A,(WORM)	;glyph: by WORM mod 8
	AND	7
	LD	E,A
	LD	D,0
	LD	HL,GLYPHS
	ADD	HL,DE
	LD	A,(HL)
	JP	PUTC

;	Choose the worm's next heading and move its head there: turn left,
;	go on or turn right at random, among the headings that stay on the
;	screen (any heading that does, in a corner). Returns the new y, x in
;	B, C and keeps the heading.
TURN:	LD	A,(WORM)
	LD	E,A
	LD	D,0
	LD	HL,WORIENT
	ADD	HL,DE
	PUSH	HL
	LD	A,(HL)
	LD	(OLDDIR),A
	LD	IX,CANDS	;the candidates
	LD	B,0		;how many
	LD	C,-1		;left, ahead, right
TURN1:	LD	A,(OLDDIR)
	ADD	A,C
	AND	7
	CALL	ONSCR
	JR	C,TURN2
	LD	(IX+0),A
	INC	IX
	INC	B
TURN2:	INC	C
	LD	A,C
	CP	2
	JR	NZ,TURN1
	LD	A,B
	OR	A
	JR	NZ,TURN4
	LD	C,0		;none: any heading that stays on the screen
TURN3:	LD	A,C
	CALL	ONSCR
	JR	C,TURN3A
	LD	(IX+0),A
	INC	IX
	INC	B
TURN3A:	INC	C
	LD	A,C
	CP	8
	JR	NZ,TURN3
TURN4:	PUSH	BC		;pick one: random mod B
	CALL	RAND
	POP	BC
TURN5:	CP	B
	JR	C,TURN6
	SUB	B
	JR	TURN5
TURN6:	LD	E,A
	LD	D,0
	LD	HL,CANDS
	ADD	HL,DE
	LD	A,(HL)
	POP	HL
	LD	(HL),A		;the new heading
	LD	E,A
	LD	HL,DX
	ADD	HL,DE
	LD	A,(HX)
	ADD	A,(HL)
	LD	C,A
	LD	HL,DY
	ADD	HL,DE
	LD	A,(HY)
	ADD	A,(HL)
	LD	B,A
	RET

;	Carry set if heading A from (HX, HY) leaves the screen; keeps A, BC.
ONSCR:	PUSH	BC
	PUSH	AF
	LD	E,A
	LD	D,0
	LD	HL,DX
	ADD	HL,DE
	LD	A,(HX)
	ADD	A,(HL)
	LD	B,A		;-1 is 255: off the screen too
	LD	A,(COLS)
	DEC	A
	CP	B
	JR	C,ONSC1
	LD	HL,DY
	ADD	HL,DE
	LD	A,(HY)
	ADD	A,(HL)
	LD	B,A
	LD	A,(ROWS)
	DEC	A
	CP	B
	JR	C,ONSC1
	POP	AF
	POP	BC
	OR	A
	RET
ONSC1:	POP	AF
	POP	BC
	SCF
	RET

;	---- output ----

;	Move the cursor to column C, row B (from 0).
GOTO:	PUSH	BC
	LD	A,ESC
	CALL	PUTC
	LD	A,'['
	CALL	PUTC
	LD	A,B
	INC	A
	CALL	PUTDEC
	LD	A,';'
	CALL	PUTC
	POP	BC
	PUSH	BC
	LD	A,C
	INC	A
	CALL	PUTDEC
	LD	A,'H'
	CALL	PUTC
	POP	BC
	RET

;	Select colour A (an SGR number, 0 for the default) unless it is the
;	one in use or -M was given.
COLOUR:	LD	HL,MONO
	BIT	0,(HL)
	RET	NZ
	LD	HL,CURCOL
	CP	(HL)
	RET	Z
	LD	(HL),A
	PUSH	AF
	LD	A,ESC
	CALL	PUTC
	LD	A,'['
	CALL	PUTC
	POP	AF
	CALL	PUTDEC
	LD	A,'m'
	JP	PUTC

;	A in decimal (0-255); keeps BC, DE.
PUTDEC:	PUSH	BC
	PUSH	DE
	LD	C,0		;digits printed
	LD	B,100
	CALL	PUTD1
	LD	B,10
	CALL	PUTD1
	ADD	A,'0'
	CALL	PUTC
	POP	DE
	POP	BC
	RET
PUTD1:	LD	D,'0'-1
PUTD2:	INC	D
	SUB	B
	JR	NC,PUTD2
	ADD	A,B
	LD	E,A
	LD	A,D
	CP	'0'
	JR	NZ,PUTD3
	LD	A,C		;no leading zeros
	OR	A
	LD	A,E
	RET	Z
	LD	A,D
PUTD3:	CALL	PUTC
	INC	C
	LD	A,E
	RET

;	The string at HL, up to a 0.
PUTS:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	HL
	JR	PUTS

;	A to the console (BDOS direct console I/O); keeps BC, DE, HL.
PUTC:	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	C,6
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	RET

;	---- data ----

;	Headings: 0 up-right, then clockwise to 7 up.
DX:	DB	1,1,1,0,-1,-1,-1,0
DY:	DB	-1,0,1,1,1,0,-1,-1
GLYPHS:	DB	'O*#$%0@~'

SASK:	DB	ESC,'[999;999H',ESC,'[6n',0
SINIT:	DB	ESC,'[0m',ESC,'[?25l',ESC,'[2J',0	;also CURCOL 0
SBYE:	DB	ESC,'[0m',ESC,'[2J',ESC,'[H',ESC,'[?25h',0
SUSAGE:	DB	'Usage: WORMS [-D ms] [-F] [-L length] [-N number] [-T] [-M]',13,10
	DB	'  -D delay per step (40)  -F field to eat  -L 2-255 (16)',13,10
	DB	'  -N 1-40 worms (3)  -T leave a trail  -M monochrome',13,10,'$'
SROOM:	DB	'Not enough memory for that many worms.',13,10,'$'

ROWS:	DB	24
COLS:	DB	80
ROWS1:	DB	0
LENGTH:	DB	16
NWORMS:	DB	3
DELMS:	DW	40
UNITMS:	DW	1
FIELD:	DB	0
TRAIL:	DB	' '
MONO:	DB	0
CURCOL:	DB	0
WORM:	DB	0
HX:	DB	0
HY:	DB	0
OLDDIR:	DB	0
RSEED:	DW	1
REFS:	DW	0
SEGS:	DW	0
TOP:	DW	0
WORIENT: DS	MAXW		;heading of each worm
WHEAD:	DS	MAXW		;head segment of each worm (follows WORIENT)
WSEGS:	DS	MAXW*2		;address of each worm's segments
CANDS:	DS	8
ROWTAB:	DS	2*255
	DS	128
STACK:
FREE:				;cell counts, then the segments

	END
