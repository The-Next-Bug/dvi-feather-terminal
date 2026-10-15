;	HWINFO: the board the machine runs on and its hardware.
;
;	HWINFO
;
;	Reads the hardware information device (port 12, src/machine/machine.h,
;	bd 1ls.9): the firmware's version and the board, then a line for
;	each record (items 16-255) the firmware has: the hardware's name,
;	whether it is there (yes / no / missing: the board should have it
;	but it did not answer / planned: the firmware does not support it
;	yet) and what it is, wrapped at 79 columns. Records it does not know
;	of yet (newer firmware) show the same way. Without the device (older
;	firmware, z80pack's picosim) port 12 reads 0FFH: HWINFO says so.
;	A screen's worth at a time: after 23 lines it shows -- More -- on
;	the 24th and waits for a key (Q, ESC or ^C: stop; any other: the
;	next 23 lines); the first page counts the command line, so it stays.
;	Works on CP/M 2.2 and 3 (the console through the BDOS only).
;
;	Z80 mnemonics; assembled with z80pack's z80asm by build.sh, put on
;	the CPM22, CPM31 and CPM32 disks by diskimg_gen.py.

BDOS	EQU	5
HWPORT	EQU	12		;the hardware information device
HWID	EQU	0		;its items: 'H', 'W', the version
HWFW	EQU	1		;the firmware, text
HWBOARD	EQU	2		;the board, text
HWREC	EQU	16		;the first record: status, name, 0, detail, 0
NAMEW	EQU	17		;the columns: name, status, detail
INDENT	EQU	26
WIDTH	EQU	79		;the last column a line uses
PAGE	EQU	23		;lines a page, the 24th row for -- More --
CR	EQU	0DH
LF	EQU	0AH

	ORG	100H

START:	LD	SP,STACK
	LD	A,HWID		;is the device there?
	OUT	(HWPORT),A
	IN	A,(HWPORT)
	CP	'H'
	JR	NZ,NODEV
	IN	A,(HWPORT)
	CP	'W'
	JR	NZ,NODEV
	IN	A,(HWPORT)	;version 1 and up
	OR	A
	JR	Z,NODEV
	CP	0FFH
	JR	Z,NODEV

	LD	HL,SFW		;Firmware, Board
	LD	A,HWFW
	CALL	TEXT
	LD	HL,SBOARD
	LD	A,HWBOARD
	CALL	TEXT
	CALL	CRLF

	LD	B,HWREC		;a line for each record
REC:	PUSH	BC
	LD	A,B
	OUT	(HWPORT),A
	IN	A,(HWPORT)	;status, 0: none
	OR	A
	JR	Z,NEXT
	LD	(STAT),A
	LD	E,0		;E: the column
	CALL	GETBUF		;the name
	CALL	PUTBUF
	LD	D,NAMEW
	CALL	PAD
	LD	A,(STAT)	;the status as a word
	DEC	A
	CP	4
	JR	C,R1
	LD	A,4		;unknown: ?
R1:	ADD	A,A
	LD	L,A
	LD	H,0
	LD	BC,STATS
	ADD	HL,BC
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	CALL	PUTSE
	LD	D,INDENT
	CALL	PAD
	CALL	GETBUF		;the detail, wrapped
	CALL	WRAP
	CALL	CRLF
NEXT:	POP	BC
	INC	B
	JR	NZ,REC
	JP	0

NODEV:	LD	HL,SNODEV
	CALL	PUTS
	JP	0

;	Label HL, then item A's text, on a line.
TEXT:	PUSH	AF
	CALL	PUTS
	POP	AF
	OUT	(HWPORT),A
	CALL	GETBUF
	LD	HL,BUF
	CALL	PUTS
	JP	CRLF

;	Read the device's next text (to its 0) into BUF: at most BUFLEN - 1
;	characters, the rest skipped.
GETBUF:	LD	HL,BUF
	LD	C,BUFLEN-1
G1:	IN	A,(HWPORT)
	OR	A
	JR	Z,G2
	CP	0FFH		;the device went? (cannot happen) stop
	JR	Z,G2
	LD	(HL),A		;when full, over the last again
	LD	A,C
	OR	A
	JR	Z,G1
	INC	HL
	DEC	C
	JR	G1
G2:	LD	(HL),0
	RET

;	BUF to the console, E counting the columns.
PUTBUF:	LD	HL,BUF
;	Text HL (to its 0) to the console, E counting the columns.
PUTSE:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	E
	INC	HL
	JR	PUTSE

;	Spaces up to column D (at least one, so columns never run together).
PAD:	LD	A,' '
	CALL	PUTC
	INC	E
	LD	A,E
	CP	D
	JR	C,PAD
	RET

;	BUF from column E (INDENT), word by word: a word that would pass
;	WIDTH starts a new line at INDENT (one longer than the room is cut
;	by the terminal). Spaces become one, written only before a word on
;	the same line.
WRAP:	LD	HL,BUF
	XOR	A
	LD	(SPC),A
W1:	LD	A,(HL)
	OR	A
	RET	Z
	CP	' '
	JR	NZ,WWORD
	LD	A,E		;a space: one before the next word, if any
	CP	INDENT
	JR	Z,W5
	LD	A,1
	LD	(SPC),A
W5:	INC	HL
	JR	W1
WWORD:	PUSH	HL		;B: the word's length
	LD	B,0
W2:	LD	A,(HL)
	OR	A
	JR	Z,W3
	CP	' '
	JR	Z,W3
	INC	B
	INC	HL
	JR	W2
W3:	POP	HL
	LD	A,(SPC)		;the space, the word: past WIDTH?
	ADD	A,E
	ADD	A,B
	CP	WIDTH+1
	JR	C,W6
	LD	A,E		;already at the line's start: as it is
	CP	INDENT
	JR	Z,W4
	CALL	CRLF
	LD	E,0
	LD	D,INDENT
	CALL	PAD0
	JR	W4
W6:	LD	A,(SPC)
	OR	A
	JR	Z,W4
	LD	A,' '
	CALL	PUTC
	INC	E
W4:	XOR	A
	LD	(SPC),A
W7:	LD	A,(HL)		;the word
	CALL	PUTC
	INC	E
	INC	HL
	DJNZ	W7
	JR	W1

;	Spaces from column E up to column D (none if there already).
PAD0:	LD	A,E
	CP	D
	RET	NC
	LD	A,' '
	CALL	PUTC
	INC	E
	JR	PAD0

;	---- output ----

;	A to the console; keeps BC, DE, HL. A page full: -- More -- first.
PUTC:	PUSH	BC
	PUSH	DE
	PUSH	HL
	PUSH	AF
	LD	A,(MORE)
	OR	A
	CALL	NZ,PAUSE
	POP	AF
	LD	E,A
	LD	C,2
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	RET

;	Text HL (to its 0) to the console.
PUTS:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	HL
	JR	PUTS

CRLF:	LD	A,CR
	CALL	PUTC
	LD	A,LF
	CALL	PUTC
	LD	A,(LINES)	;a page full: -- More -- before the next output
	INC	A		;(none if nothing follows)
	CP	PAGE
	JR	C,CR1
	LD	(MORE),A
	XOR	A
CR1:	LD	(LINES),A
	RET

;	-- More --, then a key: Q, ESC or ^C end the program, others take
;	the prompt away for the next page.
PAUSE:	XOR	A
	LD	(MORE),A
	LD	HL,SMORE
	CALL	PUTS
P1:	LD	C,6		;direct console input, no echo: 0 while none
	LD	E,0FFH
	CALL	BDOS
	OR	A
	JR	Z,P1
	PUSH	AF
	LD	HL,SERASE
	CALL	PUTS
	POP	AF
	AND	5FH		;upper case (^C, ESC: 03H, 1BH as they were)
	CP	'Q'
	JP	Z,0
	CP	3
	JP	Z,0
	CP	1BH
	JP	Z,0
	RET

;	---- data ----

STATS:	DW	SYES,SNO,SMISS,SPLAN,SUNK
SYES:	DB	'yes',0
SNO:	DB	'no',0
SMISS:	DB	'missing',0
SPLAN:	DB	'planned',0
SUNK:	DB	'?',0
SFW:	DB	'Firmware         ',0
SBOARD:	DB	'Board            ',0
SMORE:	DB	'-- More --',0
SERASE:	DB	CR,'          ',CR,0
SNODEV:	DB	'No hardware information here: the firmware (or emulator)',CR,LF
	DB	'has no device on port 12; newer DVI Feather Terminal',CR,LF
	DB	'firmware has it.',CR,LF,0

LINES:	DB	1		;lines on this page: the command line's first
MORE:	DB	0		;the page is full
STAT:	DS	1
SPC:	DS	1		;WRAP: a space before the next word
BUFLEN	EQU	128
BUF:	DS	BUFLEN
	DS	64
STACK:
	END	START
