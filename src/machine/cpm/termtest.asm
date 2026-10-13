;	TERMTEST: the terminal's simple functions, page by page, for each of
;	its personalities.
;
;	TERMTEST
;
;	A menu picks the personality the terminal is set to: VT100, VT52
;	(a VT100 switched to VT52 mode with ESC [ ? 2 l here, and back to
;	ANSI mode with ESC < at the end) or ADM-3A. Then one page each for
;	cursor addressing, cursor moves, erasing, inserting and deleting
;	lines, scrolling, wrapping at the last column, line graphics, the
;	terminal's replies (identify, cursor position) and the codes the
;	keys send. Each page says what it should look like; any key goes on
;	to the next, Esc back to the menu. Every control sequence comes from
;	the personality's column of OPS (0: it has none) and AT, so the
;	pages are the same for all three.
;
;	The menu is plain text, so it reads the same on any terminal. The
;	terminal's replies are typed straight back by the firmware, so a
;	short wait for them is enough at any CPU speed.
;
;	Assembled with z80pack's z80asm by build.sh, put on the Z80TESTS
;	disk by diskimg_gen.py.

BDOS	EQU	5
ESC	EQU	1BH
CR	EQU	0DH
LF	EQU	0AH
BS	EQU	08H
BUFMAX	EQU	32		;reply / key bytes kept

;	Operations: rows of OPS.
OCLR	EQU	0		;clear the screen, cursor home
OHOME	EQU	1
OUP	EQU	2
ODOWN	EQU	3		;never at the bottom: the ADM-3A's ^J scrolls
ORIGHT	EQU	4
OLEFT	EQU	5
OEOL	EQU	6		;erase to the end of the line
OEOS	EQU	7		;erase to the end of the screen
OIL	EQU	8		;insert a line
ODL	EQU	9		;delete a line
ORLF	EQU	10		;reverse line feed
OGON	EQU	11		;line graphics on
OGOFF	EQU	12		;line graphics off
OENTER	EQU	13		;start of the test
OLEAVE	EQU	14		;end of the test
OID	EQU	15		;identify request

	ORG	100H

START:	LD	SP,STACK
MENU:	LD	HL,SMENU
	CALL	PUTS
MENU1:	CALL	WAITK
	CP	3		;^C
	JP	Z,QUIT
	CP	'q'
	JP	Z,QUIT
	CP	'Q'
	JP	Z,QUIT
	SUB	'1'
	CP	3
	JR	NC,MENU1
	LD	(PERS),A
	LD	A,OENTER
	CALL	DO
	LD	HL,PAGES
PLOOP:	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	LD	A,D
	OR	E
	JR	Z,PDONE
	PUSH	HL
	CALL	CALLDE
	POP	HL
	JR	NC,PLOOP	;carry: Esc, back to the menu
PDONE:	LD	A,OCLR
	CALL	DO
	LD	A,OLEAVE
	CALL	DO
	JR	MENU

CALLDE:	EX	DE,HL
	JP	(HL)

QUIT:	LD	HL,SCRLF
	CALL	PUTS
	JP	0

PAGES:	DW	PADDR,PMOVES,PERASE,PLINES,PSCROLL,PWRAP,PGRAPH,PIDENT,PKEYS,0

;	---- pages: each returns carry to go back to the menu ----

;	Cursor addressing: a box and the screen's corners.
PADDR:	LD	HL,TADDR
	CALL	TITLE
	LD	A,'+'
	LD	DE,020AH	;row 2, column 10
	CALL	ATC
	LD	DE,0245H	;column 69
	CALL	ATC
	LD	DE,150AH	;row 21
	CALL	ATC
	LD	DE,1545H
	CALL	ATC
	LD	A,'-'
	LD	DE,020BH
	CALL	AT
	LD	B,58
	CALL	REPC
	LD	DE,150BH
	CALL	AT
	LD	B,58
	CALL	REPC
	LD	A,'|'
	LD	D,3
PADDR1:	LD	E,10
	CALL	ATC
	LD	E,69
	CALL	ATC
	INC	D
	LD	B,A
	LD	A,D
	CP	21
	LD	A,B
	JR	C,PADDR1
	LD	DE,0B1CH	;row 11, column 28
	LD	HL,SCENTRE
	CALL	ATS
	LD	DE,0100H
	LD	HL,STOPL
	CALL	ATS
	LD	DE,1644H	;row 22, column 68: up to the last column
	LD	HL,SBOTR
	CALL	ATS
	JP	NEXT

;	Cursor moves: each row gets ?? in column 30, then the moves under
;	test should land the OK on top of it.
PMOVES:	LD	HL,TMOVES
	CALL	TITLE
	LD	DE,0
	LD	HL,SQQ
	CALL	ATS
	LD	A,OHOME
	CALL	DO
	CALL	OK
	LD	D,2
	LD	HL,LADDR
	CALL	ROW
	LD	DE,021EH	;column 30
	CALL	AT
	CALL	OK
	LD	D,3
	LD	HL,LRIGHT
	CALL	ROW
	LD	DE,0319H	;column 25, then right 5
	CALL	AT
	LD	A,ORIGHT
	LD	B,5
	CALL	REPOP
	CALL	OK
	LD	D,4
	LD	HL,LLEFT
	CALL	ROW
	LD	DE,0423H	;column 35, then left 5
	CALL	AT
	LD	A,OLEFT
	LD	B,5
	CALL	REPOP
	CALL	OK
	LD	D,5
	LD	HL,LUP
	CALL	ROW
	LD	DE,071EH	;row 7, then up 2
	CALL	AT
	LD	A,OUP
	LD	B,2
	CALL	REPOP
	CALL	OK
	LD	D,6
	LD	HL,LDOWN
	CALL	ROW
	LD	DE,041EH	;row 4, then down 2
	CALL	AT
	LD	A,ODOWN
	LD	B,2
	CALL	REPOP
	CALL	OK
	LD	D,7
	LD	HL,LRSTOP
	CALL	ROW
	LD	DE,074BH	;column 75, right 10 (stops at 79), left 49
	CALL	AT
	LD	A,ORIGHT
	LD	B,10
	CALL	REPOP
	LD	A,OLEFT
	LD	B,49
	CALL	REPOP
	CALL	OK
	LD	D,8
	LD	HL,LLSTOP
	CALL	ROW
	LD	DE,0805H	;column 5, left 10 (stops at 0), right 30
	CALL	AT
	LD	A,OLEFT
	LD	B,10
	CALL	REPOP
	LD	A,ORIGHT
	LD	B,30
	CALL	REPOP
	CALL	OK
	LD	D,9
	LD	HL,LUSTOP
	CALL	ROW
	LD	DE,011EH	;row 1, up 5 (stops at 0), down 9
	CALL	AT
	LD	A,OUP
	LD	B,5
	CALL	REPOP
	LD	A,ODOWN
	LD	B,9
	CALL	REPOP
	CALL	OK
	LD	D,10
	LD	HL,LCR
	CALL	ROW
	LD	DE,0A32H	;column 50, CR, right 30
	CALL	AT
	LD	A,CR
	CALL	PUTC
	LD	A,ORIGHT
	LD	B,30
	CALL	REPOP
	CALL	OK
	LD	D,11
	LD	HL,LBS
	CALL	ROW
	LD	DE,0B20H	;column 32, BS twice
	CALL	AT
	LD	A,BS
	CALL	PUTC
	CALL	PUTC
	CALL	OK
	JP	NEXT

;	Erasing to the end of the line and of the screen.
PERASE:	LD	HL,TERASE
	CALL	TITLE
	LD	A,(PERS)
	CP	2
	JR	NZ,PERAS1
	LD	DE,0103H
	LD	HL,SKAYPRO
	CALL	ATS
PERAS1:	LD	DE,0200H
	LD	HL,SEOL
	CALL	ATS
	LD	DE,0304H	;row 3 only
	CALL	FILLX
	LD	DE,031EH
	CALL	AT
	LD	A,OEOL
	CALL	DO
	LD	DE,0500H
	LD	HL,SEOS
	CALL	ATS
	LD	DE,0715H	;rows 7-20
	CALL	FILLX
	LD	DE,091EH
	CALL	AT
	LD	A,OEOS
	CALL	DO
	JP	NEXT

;	Inserting and deleting lines.
PLINES:	LD	HL,TLINES
	CALL	TITLE
	LD	A,OIL
	CALL	HAS
	JR	NC,PLINE1
	LD	DE,0200H
	LD	HL,SNOIL
	CALL	ATS
	JP	NEXT
PLINE1:	LD	DE,0100H
	LD	HL,SLINES
	CALL	ATS
	LD	DE,0316H	;line 03 to line 21
	CALL	FILLN
	LD	DE,0500H
	CALL	AT
	LD	A,OIL
	CALL	DO
	LD	DE,0A00H
	CALL	AT
	LD	A,ODL
	LD	B,2
	CALL	REPOP
	JP	NEXT

;	Scrolling: a line feed on the bottom row, then a reverse line feed
;	on the top one; the title goes on afterwards.
PSCROLL:
	LD	A,OCLR
	CALL	DO
	LD	DE,0117H	;line 01 to line 22 on rows 1-22
	CALL	FILLN
	LD	DE,1700H
	CALL	AT
	LD	A,LF
	CALL	PUTC
	LD	HL,SSCRL2	;no reverse line feed: 01 went off the top
	LD	A,ORLF
	CALL	HAS
	JR	C,PSCRL1
	LD	A,OHOME
	CALL	DO
	LD	A,ORLF
	CALL	DO
	LD	HL,SSCRL1
PSCRL1:	CALL	TITLE2
	JP	NEXT

;	Wrapping at the last column.
PWRAP:	LD	HL,TWRAP
	CALL	TITLE
	LD	DE,0200H
	LD	HL,SWRAP1
	CALL	ATS
	LD	DE,0400H
	LD	HL,SROW5
	CALL	ATS
	LD	DE,044BH	;column 75
	LD	HL,SABC
	CALL	ATS
	LD	DE,0506H
	LD	HL,SROW6
	CALL	ATS
	LD	DE,0800H
	LD	HL,SWRAP2
	CALL	ATS
	LD	DE,0900H
	CALL	AT
	LD	A,'='
	LD	B,80
	CALL	REPC
	LD	HL,SCRLF
	CALL	PUTS
	LD	HL,WRAPS
	CALL	PICK
	CALL	PUTS
	JP	NEXT

;	Line graphics: a box.
PGRAPH:	LD	HL,TGRAPH
	CALL	TITLE
	LD	A,OGON
	CALL	HAS
	JR	NC,PGRAP1
	LD	DE,0200H
	LD	HL,SNOGR
	CALL	ATS
	JP	NEXT
PGRAP1:	LD	DE,0200H
	LD	HL,SGRAPH
	CALL	ATS
	LD	DE,040AH
	LD	HL,SBOX1
	CALL	ATG
	LD	DE,050AH
	LD	HL,SBOX2
	CALL	ATG
	LD	A,OGOFF		;the box's inside in letters
	CALL	DO
	LD	DE,050BH
	LD	HL,SBOXIN
	CALL	ATS
	LD	DE,060AH
	LD	HL,SBOX3
	CALL	ATG
	LD	DE,0800H
	LD	HL,SGRAFT
	CALL	ATS
	JP	NEXT

;	The terminal's replies.
PIDENT:	LD	HL,TIDENT
	CALL	TITLE
	LD	A,(PERS)
	OR	A
	JR	Z,PIDVT
	DEC	A
	JR	Z,PID52
	LD	DE,0200H
	LD	HL,SNOID
	CALL	ATS
	JP	NEXT
PIDVT:	LD	DE,040AH	;row 5, column 11 to the terminal
	CALL	AT
	LD	HL,SCPR
	CALL	PUTS
	CALL	READ
	LD	DE,0600H
	LD	HL,SGOTCPR
	CALL	ATS
	CALL	SHOW
	LD	DE,0700H
	LD	HL,SWANTCP
	CALL	ATS
	LD	A,OID
	CALL	DO
	CALL	READ
	LD	DE,0900H
	LD	HL,SGOTDA
	CALL	ATS
	CALL	SHOW
	LD	DE,0A00H
	LD	HL,SWANTDA
	CALL	ATS
	JP	NEXT
PID52:	LD	A,OID
	CALL	DO
	CALL	READ
	LD	DE,0400H
	LD	HL,SGOTID
	CALL	ATS
	CALL	SHOW
	LD	DE,0500H
	LD	HL,SWANTID
	CALL	ATS
	JP	NEXT

;	The keys: what each one sends, a line each, until ^C.
PKEYS:	LD	HL,TKEYS
	CALL	TITLE
	LD	DE,0200H
	CALL	AT
PKEYS1:	CALL	GETKEY
	LD	A,(BUFN)
	DEC	A
	JR	NZ,PKEYS2
	LD	A,(BUF)
	CP	3
	RET	Z		;^C alone (carry clear)
PKEYS2:	CALL	SHOW
	LD	HL,SEQUALS
	CALL	PUTS
	CALL	SHOWHEX
	LD	HL,SCRLF
	CALL	PUTS
	JR	PKEYS1

;	The prompt at the bottom; carry if it was Esc alone.
NEXT:	LD	DE,1700H
	LD	HL,SNEXT
	CALL	ATS
	CALL	GETKEY
	LD	A,(BUFN)
	DEC	A
	JR	NZ,NEXT1	;a sequence
	LD	A,(BUF)
	CP	ESC
	SCF
	RET	Z
NEXT1:	OR	A		;carry clear
	RET

;	---- screen helpers ----

;	Clear the screen, then TITLE2.
TITLE:	PUSH	HL
	LD	A,OCLR
	CALL	DO
	POP	HL
;	Title HL on row 0 from column 3, then the personality's name.
TITLE2:	LD	DE,0
	CALL	AT
	LD	A,OEOL
	CALL	DO
	LD	E,3
	CALL	ATS
	LD	HL,SDASH
	CALL	PUTS
	LD	HL,NAMES
	CALL	PICK
	JP	PUTS

;	Label HL at the start of row D, ?? in column 30.
ROW:	LD	E,0
	CALL	ATS
	LD	E,30
	LD	HL,SQQ
	JP	ATS

OK:	LD	HL,SOK
	JP	PUTS

;	"line NN" at the start of rows D to E - 1 (NN: the row).
FILLN:	LD	C,E
FILLN1:	LD	E,0
	CALL	AT
	LD	HL,SLINE
	CALL	PUTS
	LD	A,D
	CP	10
	JR	NC,FILLN2
	LD	A,'0'
	CALL	PUTC
	LD	A,D
FILLN2:	CALL	PUTDEC
	INC	D
	LD	A,D
	CP	C
	JR	C,FILLN1
	RET

;	40 X from column 20 of rows D to E - 1.
FILLX:	LD	C,E
FILLX1:	LD	E,20
	CALL	AT
	LD	A,'X'
	LD	B,40
	CALL	REPC
	INC	D
	LD	A,D
	CP	C
	JR	C,FILLX1
	RET

;	Move to row D, column E (from 0). Keeps every register.
AT:	PUSH	AF
	PUSH	BC
	PUSH	HL
	LD	A,ESC
	CALL	PUTC
	LD	A,(PERS)
	OR	A
	JR	NZ,AT1
	LD	A,'['		;VT100: ESC [ row ; col H, from 1
	CALL	PUTC
	LD	A,D
	INC	A
	CALL	PUTDEC
	LD	A,';'
	CALL	PUTC
	LD	A,E
	INC	A
	CALL	PUTDEC
	LD	A,'H'
	CALL	PUTC
	JR	AT3
AT1:	DEC	A		;VT52: ESC Y, ADM-3A: ESC =; row, col + 32
	LD	A,'Y'
	JR	Z,AT2
	LD	A,'='
AT2:	CALL	PUTC
	LD	A,D
	ADD	A,32
	CALL	PUTC
	LD	A,E
	ADD	A,32
	CALL	PUTC
AT3:	POP	HL
	POP	BC
	POP	AF
	RET

;	AT, then character A / string HL.
ATC:	CALL	AT
	JP	PUTC
ATS:	CALL	AT
	JP	PUTS
;	AT, then string HL in line graphics.
ATG:	CALL	AT
	LD	A,OGON
	CALL	DO
	CALL	PUTS
	LD	A,OGOFF
	JP	DO

;	Operation A's sequence for the personality; carry if it has none.
;	Keeps BC, DE, HL.
DO:	PUSH	DE
	PUSH	HL
	CALL	LOOKUP
	SCF
	JR	Z,DO1
	CALL	PUTS
	OR	A
DO1:	POP	HL
	POP	DE
	RET

;	Carry if the personality has no operation A. Keeps BC, DE, HL.
HAS:	PUSH	DE
	PUSH	HL
	CALL	LOOKUP
	SCF
	JR	Z,HAS1
	OR	A
HAS1:	POP	HL
	POP	DE
	RET

;	HL: OPS[A][PERS]; Z if it is 0.
LOOKUP:	LD	L,A
	ADD	A,A
	ADD	A,L
	LD	L,A
	LD	A,(PERS)
	ADD	A,L
	ADD	A,A
	LD	L,A
	LD	H,0
	LD	DE,OPS
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	OR	H
	RET

;	Operation A, B times.
REPOP:	PUSH	AF
	CALL	DO
	POP	AF
	DJNZ	REPOP
	RET

;	Character A, B times.
REPC:	CALL	PUTC
	DJNZ	REPC
	RET

;	HL: the personality's entry of the table of three words at HL.
PICK:	LD	A,(PERS)
	ADD	A,A
	LD	E,A
	LD	D,0
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	RET

;	---- console ----

;	A key, and whatever follows it at once (the rest of a sequence),
;	into BUF / BUFN.
GETKEY:	CALL	WAITK
	LD	(BUF),A
	LD	B,1
	JR	READ1

;	Whatever the terminal sends in a short while, into BUF / BUFN.
READ:	LD	B,0
READ1:	LD	A,B
	CP	BUFMAX
	JR	NC,READ2
	CALL	POLL
	JR	C,READ2
	LD	HL,BUF
	LD	E,B
	LD	D,0
	ADD	HL,DE
	LD	(HL),A
	INC	B
	JR	READ1
READ2:	LD	A,B
	LD	(BUFN),A
	RET

;	BUF as text: ESC, ^X for controls, xxh above 7EH, a space after each.
SHOW:	LD	A,(BUFN)
	OR	A
	JR	NZ,SHOW1
	LD	HL,SNONE
	JP	PUTS
SHOW1:	LD	B,A
	LD	HL,BUF
SHOW2:	LD	A,(HL)
	PUSH	HL
	CALL	SHOWB
	LD	A,' '
	CALL	PUTC
	POP	HL
	INC	HL
	DJNZ	SHOW2
	RET

SHOWB:	CP	ESC
	JR	NZ,SHOWB1
	LD	HL,SESC
	JP	PUTS
SHOWB1:	CP	20H
	JR	NC,SHOWB2
	PUSH	AF
	LD	A,'^'
	CALL	PUTC
	POP	AF
	ADD	A,40H
	JP	PUTC
SHOWB2:	CP	7FH
	JP	C,PUTC
	CALL	PUTHEX
	LD	A,'h'
	JP	PUTC

;	BUF in hex.
SHOWHEX:
	LD	A,(BUFN)
	LD	B,A
	LD	HL,BUF
SHOWH1:	LD	A,(HL)
	CALL	PUTHEX
	LD	A,' '
	CALL	PUTC
	INC	HL
	DJNZ	SHOWH1
	RET

;	A key from the console (waits); keeps BC, DE, HL.
WAITK:	CALL	POLL
	JR	C,WAITK
	RET

;	A character from the console in A if one comes in a short while,
;	else carry. Keeps BC, DE, HL.
POLL:	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	BC,400
POLL1:	PUSH	BC
	LD	E,0FFH
	LD	C,6
	CALL	BDOS
	POP	BC
	OR	A
	JR	NZ,POLL2
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,POLL1
	SCF
POLL2:	POP	HL
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

;	A in hex, two digits.
PUTHEX:	PUSH	AF
	RRCA
	RRCA
	RRCA
	RRCA
	CALL	PUTH1
	POP	AF
PUTH1:	AND	0FH
	ADD	A,'0'
	CP	'9'+1
	JP	C,PUTC
	ADD	A,7
	JP	PUTC

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

;	---- the personalities ----

;	Each operation's sequence: VT100, VT52, ADM-3A (0: none).
OPS:	DW	V_CLR,W_CLR,A_CLR		;OCLR
	DW	V_HOME,W_HOME,A_HOME		;OHOME
	DW	V_UP,W_UP,A_UP			;OUP
	DW	V_DOWN,W_DOWN,A_DOWN		;ODOWN
	DW	V_RIGHT,W_RIGHT,A_RIGHT		;ORIGHT
	DW	V_LEFT,W_LEFT,A_LEFT		;OLEFT
	DW	V_EOL,W_EOL,A_EOL		;OEOL
	DW	V_EOS,W_EOS,A_EOS		;OEOS
	DW	V_IL,0,A_IL			;OIL
	DW	V_DL,0,A_DL			;ODL
	DW	V_RLF,W_RLF,0			;ORLF
	DW	V_GON,W_GON,0			;OGON
	DW	V_GOFF,W_GOFF,0			;OGOFF
	DW	EMPTY,W_ENTER,EMPTY		;OENTER
	DW	EMPTY,W_LEAVE,EMPTY		;OLEAVE
	DW	V_ID,W_ID,0			;OID

V_CLR:	DB	ESC,'[H',ESC,'[2J',0
V_HOME:	DB	ESC,'[H',0
V_UP:	DB	ESC,'[A',0
V_DOWN:	DB	ESC,'[B',0
V_RIGHT: DB	ESC,'[C',0
V_LEFT:	DB	ESC,'[D',0
V_EOL:	DB	ESC,'[K',0
V_EOS:	DB	ESC,'[J',0
V_IL:	DB	ESC,'[L',0
V_DL:	DB	ESC,'[M',0
V_RLF:	DB	ESC,'M',0
V_GON:	DB	ESC,'(0',0
V_GOFF:	DB	ESC,'(B',0
V_ID:	DB	ESC,'[c',0

W_CLR:	DB	ESC,'H',ESC,'J',0
W_HOME:	DB	ESC,'H',0
W_UP:	DB	ESC,'A',0
W_DOWN:	DB	ESC,'B',0
W_RIGHT: DB	ESC,'C',0
W_LEFT:	DB	ESC,'D',0
W_EOL:	DB	ESC,'K',0
W_EOS:	DB	ESC,'J',0
W_RLF:	DB	ESC,'I',0
W_GON:	DB	ESC,'F',0
W_GOFF:	DB	ESC,'G',0
W_ENTER: DB	ESC,'[?2l',0
W_LEAVE: DB	ESC,'<',0
W_ID:	DB	ESC,'Z',0

A_CLR:	DB	1AH,0			;^Z
A_HOME:	DB	1EH,0			;^^
A_UP:	DB	0BH,0			;^K
A_DOWN:	DB	LF,0
A_RIGHT: DB	0CH,0			;^L
A_LEFT:	DB	BS,0
A_EOL:	DB	18H,0			;^X (Kaypro)
A_EOS:	DB	17H,0			;^W (Kaypro)
A_IL:	DB	ESC,'E',0		;Kaypro
A_DL:	DB	ESC,'R',0		;Kaypro
EMPTY:	DB	0

NAMES:	DW	SVT100,SVT52,SADM
SVT100:	DB	'VT100',0
SVT52:	DB	'VT52 mode of a VT100',0
SADM:	DB	'ADM-3A',0

WRAPS:	DW	SWRAPV,SWRAPV,SWRAPA
SWRAPV:	DB	'This line follows at once: the wrap waited for a character',0
SWRAPA:	DB	'After a blank row: the ADM-3A wrapped at once',0

;	---- text ----

SMENU:	DB	CR,LF,'TERMTEST: the terminal personalities',CR,LF
	DB	'1 VT100   2 VT52 (a VT100 switched by ESC [ ? 2 l, back by ESC <)'
	DB	'   3 ADM-3A   Q quit',CR,LF
	DB	'Choice? ',0
SCRLF:	DB	CR,LF,0
SNEXT:	DB	'Any key: next page   Esc: back to the menu',0
SDASH:	DB	' -- ',0
SQQ:	DB	'??',0
SOK:	DB	'OK',0
SLINE:	DB	'line ',0
SESC:	DB	'ESC',0
SNONE:	DB	'(nothing)',0
SEQUALS: DB	'  = ',0

TADDR:	DB	'Cursor addressing: a box, its centre, two corners',0
SCENTRE: DB	'centre (row 12, col 29)',0
STOPL:	DB	'top left (row 2)',0
SBOTR:	DB	'bottom right',0

TMOVES:	DB	'Moves: OK on every row, no question marks',0
LADDR:	DB	'address',0
LRIGHT:	DB	'right 5',0
LLEFT:	DB	'left 5',0
LUP:	DB	'up 2',0
LDOWN:	DB	'down 2',0
LRSTOP:	DB	'right stops at the edge',0
LLSTOP:	DB	'left stops at the edge',0
LUSTOP:	DB	'up stops at the top',0
LCR:	DB	'carriage return',0
LBS:	DB	'backspace',0

TERASE:	DB	'Erasing',0
SKAYPRO: DB	'(Kaypro additions ^X and ^W: a real ADM-3A has neither)',0
SEOL:	DB	'To the end of the line: 10 X below, nothing after them',0
SEOS:	DB	'To the end of the screen: 2 rows of 40 X, a row of 10 X, nothing below',0

TLINES:	DB	'Insert and delete line',0
SNOIL:	DB	'A VT52 has neither.',0
SLINES:	DB	'Expect line 03, 04, a blank row, 05 to 08, 11 to 21 (09 and 10 deleted)',0

SSCRL1:	DB	'Scrolled: line 01 on row 2, line 22 on row 23',0
SSCRL2:	DB	'Scrolled: line 02 on row 2, line 22 on row 22 (no reverse LF)',0

TWRAP:	DB	'Wrapping at the last column',0
SWRAP1:	DB	'ABCDE ends row 5, FGHIJ starts row 6',0
SROW5:	DB	'row 5 ->',0
SABC:	DB	'ABCDEFGHIJ',0
SROW6:	DB	'<- row 6',0
SWRAP2:	DB	'80 characters, then CR LF:',0

TGRAPH:	DB	'Line graphics',0
SNOGR:	DB	'An ADM-3A has none.',0
SGRAPH:	DB	'A box drawn with lines, not letters:',0
SBOX1:	DB	'lqqqqqqk',0
SBOX2:	DB	'x      x',0
SBOXIN:	DB	' box  ',0
SBOX3:	DB	'mqqqqqqj',0
SGRAFT:	DB	'Letters again after it: lqk',0

TIDENT:	DB	'Replies to the host',0
SNOID:	DB	'An ADM-3A sends none: no identify, no cursor report.',0
SCPR:	DB	ESC,'[6n',0
SGOTCPR: DB	'Cursor position report (asked at row 5, col 11): ',0
SWANTCP: DB	'                                       expected: ESC [ 5 ; 1 1 R',0
SGOTDA:	DB	'Device attributes (ESC [ c): ',0
SWANTDA: DB	'                  expected: ESC [ ? 1 ; 2 c',0
SGOTID:	DB	'Identify (ESC Z): ',0
SWANTID: DB	'         expected: ESC / Z',0

TKEYS:	DB	'Keys: what each one sends. Ctrl-C ends.',0

PERS:	DB	0		;0 VT100, 1 VT52, 2 ADM-3A
BUFN:	DB	0
BUF:	DS	BUFMAX
	DS	64
STACK:

	END
