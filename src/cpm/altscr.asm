;	ALTSCR: the alternate screen (xterm's ESC [ ? 1049 / 1047 / 47 h
;	and l), step by step.
;
;	ALTSCR
;
;	The main screen is filled with numbered rows in bold, underlined
;	yellow and reverse video, with an X in the middle. Each step goes to
;	the alternate screen with one of the three modes, scrolls it, says
;	what to look for and comes back on a key: the main screen must be as
;	it was. ?1049 also saves and restores the cursor and its colours, so
;	"<1049" comes back right of the X in bold red; ?1047 and ?47 leave the
;	cursor where the alternate screen had it. The last step asks for
;	SET-UP over the alternate screen, whose rows must come back when it
;	closes (at 80 columns; at 132 there is no room to save them and they
;	come back blank). Any key goes on; Ctrl-C leaves at once.
;
;	For the VT100 personality (an ADM-3A or a VT52 has no alternate
;	screen: they show the escape sequences' letters). Assembled with
;	z80pack's z80asm by build.sh, put on the Z80TESTS disk by
;	diskimg_gen.py.

BDOS	EQU	5
ESC	EQU	1BH
CR	EQU	0DH
LF	EQU	0AH

	ORG	100H

START:	LD	SP,STACK
	CALL	MAIN
	LD	HL,STEPS
SLOOP:	LD	A,(HL)		;the flag byte; 0FFH ends the list
	CP	0FFH
	JR	Z,DONE
	CALL	STEP		;HL: past the step
	JR	SLOOP
DONE:	LD	DE,1701H	;row 23
	CALL	CUP
	LD	HL,SDONE
	CALL	PUTS
	CALL	WAITK
QUIT:	LD	HL,SQUIT
	CALL	PUTS
	JP	0

;	The main screen: rows 1-22 numbered in four styles, an X at row 12,
;	column 40, and the help on row 24.
MAIN:	LD	HL,SCLR
	CALL	PUTS
	LD	B,1		;row
MAIN1:	LD	D,B
	LD	E,1
	CALL	CUP
	LD	A,B
	AND	3
	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,STYLES
	ADD	HL,DE
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	EX	DE,HL
	CALL	PUTS
	LD	HL,SROW
	CALL	PUTS
	LD	A,B
	CALL	PUTDEC
	LD	HL,SROWT
	CALL	PUTS
	INC	B
	LD	A,B
	CP	23
	JR	C,MAIN1
	LD	DE,0C28H	;row 12, column 40
	CALL	CUP
	LD	A,'X'
	CALL	PUTC
	LD	DE,1801H	;row 24
	CALL	CUP
	LD	HL,SHELP
	JP	PUTS

;	One step at HL: a flag byte (1: ?1049, write the label at the
;	restored cursor), the mode's digits, the alternate screen's text and
;	the message for row 23 once back, each ending in 0. Returns HL past
;	it.
STEP:	LD	A,(HL)
	LD	(FLAG),A
	INC	HL
	LD	(MODE),HL
	CALL	SKIP
	LD	(TEXT),HL
	CALL	SKIP
	PUSH	HL		;the message
	CALL	SKIP
	PUSH	HL		;the next step
	; The cursor and colours ?1049 saves: right of the X, bold red.
	LD	DE,0C29H	;row 12, column 41
	CALL	CUP
	LD	HL,SRED
	CALL	PUTS
	LD	A,'h'
	CALL	MODESEQ
	LD	HL,SPLAIN
	CALL	PUTS
	; Scroll the alternate screen: six lines in at the bottom.
	LD	DE,1801H	;row 24
	CALL	CUP
	LD	B,1
STEP1:	LD	HL,SSCRL
	CALL	PUTS
	LD	A,B
	CALL	PUTDEC
	INC	B
	LD	A,B
	CP	7
	JR	C,STEP1
	LD	DE,0101H
	CALL	CUP
	LD	HL,(TEXT)
	CALL	PUTS
	CALL	WAITK
	LD	A,'l'
	CALL	MODESEQ
	LD	A,(FLAG)
	OR	A
	JR	Z,STEP2
	LD	HL,SLABEL	;at the restored cursor, in its colours
	CALL	PUTS
STEP2:	LD	HL,SPLAIN
	CALL	PUTS
	LD	DE,1701H	;row 23
	CALL	CUP
	LD	HL,SEL
	CALL	PUTS
	POP	DE		;the next step
	POP	HL		;the message
	PUSH	DE
	CALL	PUTS
	CALL	WAITK
	POP	HL
	RET

;	HL past the string at HL and its 0.
SKIP:	LD	A,(HL)
	INC	HL
	OR	A
	JR	NZ,SKIP
	RET

;	ESC [ ? <the step's mode> A
MODESEQ: PUSH	AF
	LD	HL,SCSIQ
	CALL	PUTS
	LD	HL,(MODE)
	CALL	PUTS
	POP	AF
	JP	PUTC

;	Cursor to row D, column E (from 1).
CUP:	LD	A,ESC
	CALL	PUTC
	LD	A,'['
	CALL	PUTC
	LD	A,D
	CALL	PUTDEC
	LD	A,';'
	CALL	PUTC
	LD	A,E
	CALL	PUTDEC
	LD	A,'H'
	JP	PUTC

;	Wait for a key; Ctrl-C quits. The rest of a key's sequence (an
;	arrow key sends three bytes) is dropped, so it is one step too.
WAITK:	PUSH	BC
	PUSH	DE
	PUSH	HL
WAITK1:	LD	E,0FFH
	LD	C,6
	CALL	BDOS
	OR	A
	JR	Z,WAITK1
	CP	3
	JP	Z,QUIT
	LD	BC,400		;a short while
WAITK2:	PUSH	BC
	LD	E,0FFH
	LD	C,6
	CALL	BDOS
	POP	BC
	CP	3
	JP	Z,QUIT
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,WAITK2
	POP	HL
	POP	DE
	POP	BC
	RET

;	A in decimal; keeps BC, DE, HL.
PUTDEC:	PUSH	BC
	PUSH	HL
	LD	L,0		;L: a digit printed
	LD	B,100
	CALL	PUTD1
	LD	B,10
	CALL	PUTD1
	ADD	A,'0'
	CALL	PUTC
	POP	HL
	POP	BC
	RET
PUTD1:	LD	H,'0'
PUTD2:	SUB	B
	JR	C,PUTD3
	INC	H
	JR	PUTD2
PUTD3:	ADD	A,B
	LD	C,A
	LD	A,H
	CP	'0'
	JR	NZ,PUTD4
	INC	L
	DEC	L
	JR	Z,PUTD5		;a leading zero
PUTD4:	CALL	PUTC
	LD	L,1
PUTD5:	LD	A,C
	RET

;	The string at HL (to its 0) to the console; HL ends on the 0.
PUTS:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	HL
	JR	PUTS

;	A to the console (BDOS direct console I/O); keeps BC, DE, HL.
PUTC:	PUSH	AF
	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	C,6
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	POP	AF
	RET

;	---- the steps ----

STEPS:	DB	1,'1049',0
	DB	'ALTERNATE SCREEN (ESC [ ? 1049 h)',CR,LF,CR,LF
	DB	'This screen started blank; the six "scrolled" lines at the bottom',CR,LF
	DB	'scrolled it, not the main screen.',CR,LF,CR,LF
	DB	'Any key: back with ESC [ ? 1049 l. The main screen comes back as it was,',CR,LF
	DB	'and the cursor too: "<1049" appears right of the X, in bold red.',0
	DB	'Back from ?1049: rows 1-22 and the X unchanged, "<1049" after the X',CR,LF
	DB	'in bold red. Any key: next step.',0

	DB	0,'1047',0
	DB	'ALTERNATE SCREEN (ESC [ ? 1047 h)',CR,LF,CR,LF
	DB	'Blank again (nothing left from the ?1049 step), then scrolled.',CR,LF,CR,LF
	DB	'Any key: back with ESC [ ? 1047 l. The main screen comes back as it was;',CR,LF
	DB	'this mode does not save the cursor, so nothing is written at the X.',0
	DB	'Back from ?1047: the main screen unchanged since the last step.',CR,LF
	DB	'Any key: next step.',0

	DB	0,'47',0
	DB	'ALTERNATE SCREEN (ESC [ ? 47 h)',CR,LF,CR,LF
	DB	'The oldest mode; here it works as ?1047 does.',CR,LF,CR,LF
	DB	'Any key: back with ESC [ ? 47 l.',0
	DB	'Back from ?47: the main screen unchanged since the last step.',CR,LF
	DB	'Any key: next step.',0

	DB	0,'1049',0
	DB	'ALTERNATE SCREEN with SET-UP over it',CR,LF,CR,LF
	DB	'Open SET-UP now (F12 or Pause) and close it with Esc: these lines',CR,LF
	DB	'and the scrolled ones must come back (at 132 columns they come back',CR,LF
	DB	'blank: no room to save them).',CR,LF,CR,LF
	DB	'Then any key: back with ESC [ ? 1049 l.',0
	DB	'Back from SET-UP over the alternate screen: the main screen unchanged.',CR,LF
	DB	'Any key: next step.',0

	DB	0FFH

SCLR:	DB	ESC,'[0m',ESC,'[H',ESC,'[2J',0
SPLAIN:	DB	ESC,'[0m',0
SRED:	DB	ESC,'[1;31m',0
SEL:	DB	ESC,'[K',ESC,'[1B',ESC,'[K',ESC,'[1A',0	;rows 23 and 24
SCSIQ:	DB	ESC,'[?',0
SQUIT:	DB	ESC,'[?1049l',ESC,'[0m',ESC,'[H',ESC,'[2J',0
STYLES:	DW	STY0,STY1,STY2,STY3
STY0:	DB	0
STY1:	DB	ESC,'[1m',0
STY2:	DB	ESC,'[4;33m',0
STY3:	DB	ESC,'[7m',0
SROW:	DB	'Main screen row ',0
SROWT:	DB	ESC,'[0m  the quick brown fox jumps over the lazy dog',0
SHELP:	DB	'ALTSCR: the alternate screen. Any key: next step. Ctrl-C quits.',0
SSCRL:	DB	CR,LF,'scrolled ',0
SLABEL:	DB	'<1049',0
SDONE:	DB	'All steps done. Any key: quit (the screen is cleared).',0

FLAG:	DB	0
MODE:	DW	0
TEXT:	DW	0
	DS	64
STACK:

	END
