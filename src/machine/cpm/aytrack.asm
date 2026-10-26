;	AYTRACK: a tracker for the AY-3-8910 sound chip.
;
;	AYTRACK [d:][NAME[.AYT]]	(default SONG.AYT)
;
;	Edits a song (bd 1ls.18.11) on a VT100 screen: up to 16 patterns of
;	32 rows, each row a note and an instrument on channels A, B and C;
;	an order list of up to 64 patterns; 8 instruments, each 16 volume
;	steps (one a frame, the last held), tone and/or noise and a noise
;	period; a speed (frames a row, 50 frames a second). Space plays the
;	song from the selected order, Return the pattern on screen, both
;	looping, while the editing goes on; ^S saves NAME.AYT, ^E writes
;	NAME.PSG for AYPLAY, ^Q quits. Tab moves between the pattern, the
;	order list and the instruments. The help lines at the bottom list
;	the keys, a table under the instruments which key plays which note.
;
;	NAME.AYT: the SONG area as it is in memory (26 records): "AYT1",
;	the speed, the order list's length, 64 orders, 8 instruments of 18
;	bytes (16 volumes, mode: bit 0 tone, bit 1 noise; noise period), 16
;	patterns of 32 rows x 3 channels x (note, instrument). A note is
;	octave x 12 + semitone (12-95: C1 to B7), 0 empty, FFH off.
;
;	The chip is on RC2014's ports (D8h selects a register, D0h writes
;	it), as for AYTEST and AYPLAY; without it (or without the firmware's
;	millisecond tick, ports 68/69) AYTRACK still edits, saves and
;	exports. Works on CP/M 2.2 and 3; prints through BDOS 6, as it reads
;	keys (AYTEST's note).
;
;	Z80 mnemonics; assembled with z80pack's z80asm by build.sh, put on
;	the CPM22 and CPM32 disks by diskimg_gen.py.

BDOS	EQU	5
FCB	EQU	5CH
AYSEL	EQU	0D8H
AYDAT	EQU	0D0H
TICKL	EQU	68
TICKH	EQU	69
NROWS	EQU	32		;rows a pattern
NPATS	EQU	16
NINST	EQU	8
NORD	EQU	64
ISIZE	EQU	18		;an instrument's bytes
PSIZE	EQU	192		;a pattern's: 32 rows x 6
VROWS	EQU	16		;rows on screen
TOP	EQU	3		;the screen row of the first
MSGROW	EQU	20
CR	EQU	0DH
LF	EQU	0AH
ESC	EQU	1BH
KUP	EQU	80H		;keys: cursor and editing
KDOWN	EQU	81H
KRIGHT	EQU	82H
KLEFT	EQU	83H
KDEL	EQU	84H
KINS	EQU	85H
SONG	EQU	3000H		;the song (saved as it is), above the program
SONGSZ	EQU	26*128
SSPEED	EQU	SONG+4
SOLEN	EQU	SONG+5
SORDER	EQU	SONG+6
SINST	EQU	SORDER+NORD
SPAT	EQU	SINST+NINST*ISIZE

	ORG	100H

START:	LD	SP,STACK
	CALL	PROBE
	JR	NZ,ST1
	LD	A,1
	LD	(HASAY),A
	CALL	QUIET
	CALL	TICKOK
	JR	NZ,ST1
	LD	A,1
	LD	(HASTK),A
ST1:	LD	A,(FCB)		;the file: SONG.AYT without a name
	LD	(FDRV),A
	LD	HL,FCB+1
	LD	A,(HL)
	CP	' '
	JR	NZ,ST2
	LD	HL,DEFNAM
ST2:	LD	DE,FNAME
	LD	BC,8
	LDIR
	CALL	NEWSONG
	CALL	LOAD
	CALL	DRAWALL
MAIN:	CALL	PLAYTK
	CALL	RDKEY
	OR	A
	JR	Z,MAIN
	CALL	DOKEY
	JR	MAIN

;	---- keys ----

DOKEY:	CP	11H		;^Q
	JP	Z,QUIT
	CP	13H		;^S
	JP	Z,SAVE
	CP	05H		;^E
	JP	Z,EXPORT
	CP	09H		;Tab: the next panel
	JR	NZ,DK1
	LD	A,(FOCUS)
	INC	A
	CP	3
	JR	C,DK0
	XOR	A
DK0:	LD	(FOCUS),A
	JP	DRAWPAN
DK1:	CP	' '		;the song from the selected order
	JR	NZ,DK2
	LD	A,(PMODE)
	OR	A
	JP	NZ,STOP
	LD	A,1
	JP	PLAY
DK2:	CP	CR		;the pattern on screen
	JR	NZ,DK3
	LD	A,(PMODE)
	OR	A
	JP	NZ,STOP
	LD	A,2
	JP	PLAY
DK3:	CP	'['		;octave
	JR	NZ,DK4
	LD	A,(OCTAVE)
	CP	2
	RET	C
	DEC	A
	JR	DK5
DK4:	CP	']'
	JR	NZ,DK6
	LD	A,(OCTAVE)
	CP	6
	RET	NC
	INC	A
DK5:	LD	(OCTAVE),A
	JP	DRAWHDR
DK6:	CP	'<'		;speed
	JR	NZ,DK7
	LD	A,(SSPEED)
	CP	2
	RET	C
	DEC	A
	JR	DK8
DK7:	CP	'>'
	JR	NZ,DK9
	LD	A,(SSPEED)
	CP	15
	RET	NC
	INC	A
DK8:	LD	(SSPEED),A
	JP	DRAWHDR
DK9:	CP	0EH		;^N, ^P: the pattern on screen
	JR	NZ,DK10
	LD	A,(EPAT)
	INC	A
	JR	DK11
DK10:	CP	10H
	JR	NZ,DK12
	LD	A,(EPAT)
	DEC	A
DK11:	AND	0FH
	LD	(EPAT),A
	CALL	DRAWHDR
	JP	DRAWPAT
DK12:	LD	C,A		;the panel's own keys
	LD	A,(FOCUS)
	OR	A
	LD	A,C
	JP	Z,PATKEY
	LD	A,(FOCUS)
	DEC	A
	LD	A,C
	JP	Z,ORDKEY
	JP	INSKEY

;	The pattern: the cursor, notes, instruments.
PATKEY:	CP	KUP
	JR	NZ,PK1
	LD	A,(CROW)
	DEC	A
	JR	PK2
PK1:	CP	KDOWN
	JR	NZ,PK3
	LD	A,(CROW)
	INC	A
PK2:	AND	NROWS-1
	JP	MOVROW
PK3:	CP	KLEFT
	JR	NZ,PK4
	LD	A,(CPOS)
	OR	A
	RET	Z
	DEC	A
	JR	PK5
PK4:	CP	KRIGHT
	JR	NZ,PK6
	LD	A,(CPOS)
	CP	5
	RET	NC
	INC	A
PK5:	LD	(CPOS),A
	LD	A,(CROW)
	JP	DRAWROW
PK6:	LD	C,A
	LD	A,(CPOS)
	AND	1
	LD	A,C
	JR	NZ,PK20		;the instrument column
	CP	'='		;note off
	JR	NZ,PK7
	LD	B,0FFH
	JR	PK9
PK7:	CALL	ISCLR
	JR	NZ,PK8
	CALL	CELLAD
	XOR	A
	LD	(HL),A
	INC	HL
	LD	(HL),A
	JR	PK10
PK8:	CALL	NOTEKY		;a note key: A = its note
	RET	C
	LD	B,A
PK9:	LD	A,B
	LD	(NOTEV),A
	CALL	CELLAD
	LD	A,(NOTEV)
	LD	(HL),A
	INC	HL
	LD	A,(CURINS)
	LD	(HL),A
	LD	(PVINS),A
	LD	A,(NOTEV)	;heard, when nothing plays
	CALL	PREVIEW
PK10:	LD	A,(CROW)	;the next row
	INC	A
	AND	NROWS-1
	JP	MOVROW
PK20:	CALL	ISCLR
	JR	NZ,PK21
	XOR	A
	JR	PK22
PK21:	SUB	'0'
	RET	C
	CP	NINST
	RET	NC
	LD	(CURINS),A
PK22:	LD	(NOTEV),A
	CALL	CELLAD
	INC	HL
	LD	A,(NOTEV)
	LD	(HL),A
	CALL	DRAWINS
	CALL	DRAWHDR
	JR	PK10

;	Z if A clears a cell: '.', BS, DEL or Delete.
ISCLR:	CP	'.'
	RET	Z
	CP	08H
	RET	Z
	CP	7FH
	RET	Z
	CP	KDEL
	RET

;	The cursor to row A (scrolling the pattern if need be).
MOVROW:	LD	B,A
	LD	A,(CROW)
	LD	C,A
	LD	A,B
	LD	(CROW),A
	LD	A,(PTOP)
	LD	D,A
	LD	A,B
	CP	D
	JR	C,MR1		;above the top
	SUB	D
	CP	VROWS
	JR	C,MR2		;on screen
	LD	A,B
	SUB	VROWS-1
	JR	MR3
MR1:	LD	A,B
MR3:	LD	(PTOP),A
	JP	DRAWPAT
MR2:	LD	A,C		;the old row and the new
	CALL	DRAWROW
	LD	A,(CROW)
	JP	DRAWROW

;	HL = the cursor's cell in the pattern on screen.
CELLAD:	LD	A,(EPAT)
	LD	B,A
	LD	A,(CROW)
	LD	C,A
	LD	A,(CPOS)
	SRL	A
	JP	CELL

;	HL = pattern B's row C, channel A. Keeps BC, DE.
CELL:	PUSH	DE
	PUSH	AF
	LD	L,B		;B x 192
	LD	H,0
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	LD	D,H
	LD	E,L
	ADD	HL,HL
	ADD	HL,DE
	LD	A,C		;+ the row x 6
	ADD	A,A
	LD	E,A
	ADD	A,A
	ADD	A,E
	LD	E,A
	LD	D,0
	ADD	HL,DE
	POP	AF		;+ the channel x 2
	ADD	A,A
	LD	E,A
	ADD	HL,DE
	LD	DE,SPAT
	ADD	HL,DE
	POP	DE
	RET

;	A = the note for key A (the current octave; carry: not a note key).
NOTEKY:	CP	'A'
	JR	C,NK1
	CP	'Z'+1
	JR	NC,NK1
	ADD	A,20H		;lower case
NK1:	LD	HL,KEYTAB
	LD	B,24
NK2:	CP	(HL)
	JR	Z,NK3
	INC	HL
	DJNZ	NK2
	SCF
	RET
NK3:	LD	A,24		;its offset: 24 - B
	SUB	B
	LD	B,A
	LD	A,(OCTAVE)
	LD	C,A
	ADD	A,A		;x 12
	ADD	A,C
	ADD	A,A
	ADD	A,A
	ADD	A,B
	OR	A		;no carry
	RET

;	The order list: the selection, its pattern, inserting and deleting.
ORDKEY:	CP	KUP
	JR	NZ,OK1
	LD	A,(EORDP)
	OR	A
	RET	Z
	DEC	A
	JR	OK3
OK1:	CP	KDOWN
	JR	NZ,OK4
	LD	A,(SOLEN)
	LD	B,A
	LD	A,(EORDP)
	INC	A
	CP	B
	RET	NC
OK3:	LD	(EORDP),A	;its pattern on screen
	CALL	ORDTOP
	CALL	ORDPE
	CALL	DRAWHDR
	CALL	DRAWPAT
	JP	DRAWORD
OK4:	CP	KINS		;I, Insert: a copy after it
	JR	Z,OK5
	AND	5FH
	CP	'I'
	JR	NZ,OK8
OK5:	LD	A,(SOLEN)
	CP	NORD
	RET	NC
	LD	B,A		;the entries after it move up one
	LD	A,(EORDP)
	LD	C,A
	LD	A,B
	DEC	A
	SUB	C
	JR	Z,OK6
	PUSH	AF
	LD	A,B
	DEC	A
	CALL	ORDAD
	LD	D,H
	LD	E,L
	INC	DE
	POP	AF
	LD	C,A
	LD	B,0
	LDDR
OK6:	LD	HL,SOLEN
	INC	(HL)
	LD	A,(EORDP)
	CALL	ORDAD
	LD	A,(HL)
	INC	HL
	LD	(HL),A
	LD	A,(EORDP)
	INC	A
	JR	OK3
OK8:	CP	'X'		;X, Delete: removed
	JR	Z,OK9
	LD	A,C
	CP	KDEL
	JR	Z,OK9
	CP	7FH
	JR	NZ,OK12
OK9:	LD	A,(SOLEN)
	CP	2
	RET	C
	DEC	A
	LD	(SOLEN),A
	LD	B,A		;the entries after it move down one
	LD	A,(EORDP)
	LD	C,A
	LD	A,B
	SUB	C
	JR	Z,OK10
	PUSH	AF
	LD	A,(EORDP)
	CALL	ORDAD
	LD	D,H
	LD	E,L
	INC	HL
	POP	AF
	LD	C,A
	LD	B,0
	LDIR
OK10:	LD	A,(SOLEN)
	LD	B,A
	LD	A,(EORDP)
	CP	B
	JP	C,OK3
	DEC	A
	JP	OK3
OK12:	LD	A,C		;a hex digit: its pattern
	CALL	HEXD
	RET	C
	LD	B,A
	LD	A,(EORDP)
	CALL	ORDAD
	LD	(HL),B
	LD	A,B
	LD	(EPAT),A
	CALL	DRAWORD
	CALL	DRAWHDR
	JP	DRAWPAT

;	EPAT = the selected order's pattern.
ORDPE:	LD	A,(EORDP)
	CALL	ORDAD
	LD	A,(HL)
	AND	0FH
	LD	(EPAT),A
	RET

;	HL = order A's entry.
ORDAD:	LD	E,A
	LD	D,0
	LD	HL,SORDER
	ADD	HL,DE
	RET

;	OTOP so that EORDP is on screen.
ORDTOP:	LD	A,(OTOP)
	LD	B,A
	LD	A,(EORDP)
	CP	B
	JR	C,OT1
	SUB	B
	CP	VROWS
	RET	C
	LD	A,(EORDP)
	SUB	VROWS-1
	JR	OT2
OT1:	LD	A,(EORDP)
OT2:	LD	(OTOP),A
	RET

;	The instruments: the selection, a field, its value.
INSKEY:	CP	KUP
	JR	NZ,IK1
	LD	A,(CURINS)
	OR	A
	RET	Z
	DEC	A
	JR	IK2
IK1:	CP	KDOWN
	JR	NZ,IK3
	LD	A,(CURINS)
	CP	NINST-1
	RET	NC
	INC	A
IK2:	LD	(CURINS),A
	CALL	DRAWHDR
	JP	DRAWINS
IK3:	CP	KLEFT
	JR	NZ,IK4
	LD	A,(IFIELD)
	OR	A
	RET	Z
	DEC	A
	JR	IK5
IK4:	CP	KRIGHT
	JR	NZ,IK6
	LD	A,(IFIELD)
	CP	17
	RET	NC
	INC	A
IK5:	LD	(IFIELD),A
	JP	DRAWINS
IK6:	CALL	HEXD
	RET	C
	LD	B,A
	CALL	INSAD		;HL = the instrument
	LD	A,(IFIELD)
	LD	E,A
	LD	D,0
	ADD	HL,DE
	CP	16
	JR	NC,IK7
	LD	(HL),B		;a volume: then the next
	LD	A,(IFIELD)
	CP	15
	JR	NC,IK9
	INC	A
	LD	(IFIELD),A
	JR	IK9
IK7:	JR	NZ,IK8
	LD	A,B		;the mode: 0-3
	AND	3
	LD	(HL),A
	JR	IK9
IK8:	LD	A,(HL)		;the noise: digits shift in
	ADD	A,A
	ADD	A,A
	ADD	A,A
	ADD	A,A
	OR	B
	AND	1FH
	LD	(HL),A
IK9:	CALL	DRAWINS
	LD	A,(CURINS)	;heard: C4
	LD	(PVINS),A
	LD	A,48
	JP	PREVIEW

;	HL = instrument CURINS.
INSAD:	LD	A,(CURINS)
;	HL = instrument A.
INSA:	LD	L,A
	LD	H,0
	ADD	HL,HL
	LD	D,H
	LD	E,L
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,DE
	LD	DE,SINST
	ADD	HL,DE
	RET

;	A = the hex digit A (either case); carry if not one.
HEXD:	CP	'0'
	RET	C
	CP	'9'+1
	JR	NC,HX1
	SUB	'0'
	RET
HX1:	AND	5FH
	CP	'A'
	RET	C
	CP	'F'+1
	CCF
	RET	C
	SUB	'A'-10
	RET

;	A key, decoded (ESC [ A and so on: KUP...); 0 while none.
RDKEY:	CALL	CONIN
	OR	A
	RET	Z
	CP	ESC
	RET	NZ
	CALL	WAITC
	CP	'['
	JR	Z,RK1
	CP	'O'
	JR	Z,RK1
	LD	A,ESC		;a lone ESC
	RET
RK1:	CALL	WAITC
	CP	'A'
	JR	C,RK2
	SUB	'A'
	CP	4
	JR	NC,RK4
	LD	E,A
	LD	D,0
	LD	HL,ARROWS
	ADD	HL,DE
	LD	A,(HL)
	RET
RK2:	PUSH	AF		;ESC [ n ~
	CALL	WAITC
	POP	AF
	LD	B,A
	CP	'3'
	LD	A,KDEL
	RET	Z
	LD	A,B
	CP	'2'
	LD	A,KINS
	RET	Z
RK4:	XOR	A
	RET

;	The next character, waiting a little for it (0 if none came).
WAITC:	LD	DE,3000
WC1:	PUSH	DE
	CALL	CONIN
	POP	DE
	OR	A
	RET	NZ
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,WC1
	RET

;	A = a key, 0 while none.
CONIN:	LD	C,6
	LD	E,0FFH
	JP	BDOS

;	---- playing ----

;	Start the song (A 1, from the selected order) or the pattern on
;	screen (A 2), looping.
PLAY:	LD	B,A
	LD	A,(HASTK)
	OR	A
	LD	HL,SNOSND
	JP	Z,MSG
	LD	A,(HASAY)
	OR	A
	JP	Z,MSG
	LD	A,B
	LD	(PMODE),A
	CALL	PINIT
	LD	A,(PMODE)
	CP	1
	LD	A,(EPAT)
	JR	NZ,PL1
	LD	A,(EORDP)
	LD	(PORD),A
	CALL	ORDPAT
PL1:	LD	(PPAT),A
	CALL	ROWST
	CALL	TICK
	LD	(PDUE),HL
	CALL	DRAWPAN
	JP	DRAWHDR

STOP:	XOR	A
	LD	(PMODE),A
	CALL	QUIET
	CALL	DRAWPAN
	JP	DRAWHDR

;	The player from the top: channels silent, every register written.
PINIT:	XOR	A
	LD	(PROW),A
	LD	(PFRM),A
	LD	(PORD),A
	LD	(PENDF),A
	LD	HL,CHAN
	LD	B,12
PI1:	LD	(HL),0FFH	;silent (the step)
	INC	HL
	DJNZ	PI1
	LD	HL,SHAD
	LD	B,14
PI2:	LD	(HL),0FFH
	INC	HL
	DJNZ	PI2
	RET

;	A = order PORD's pattern.
ORDPAT:	LD	A,(PORD)
	CALL	ORDAD
	LD	A,(HL)
	AND	0FH
	RET

;	A frame, when it is due (50 a second on the tick), and the play
;	markers moved with the rows.
PLAYTK:	LD	A,(PMODE)
	OR	A
	RET	Z
	LD	A,(HASTK)
	OR	A
	RET	Z
	CALL	TICK
	LD	DE,(PDUE)
	OR	A
	SBC	HL,DE
	BIT	7,H
	RET	NZ		;not yet
	LD	A,H
	OR	A
	JR	NZ,PT1
	LD	A,L
	CP	200
	JR	C,PT2
PT1:	CALL	TICK		;far behind: from now
	LD	(PDUE),HL
PT2:	LD	HL,(PDUE)
	LD	DE,20
	ADD	HL,DE
	LD	(PDUE),HL
	XOR	A
	LD	(OUTMOD),A
	CALL	FRAME
	LD	A,(PROW)
	LD	(OROW),A
	LD	A,(PORD)
	LD	(OORD),A
	LD	A,(PPAT)
	LD	(OPAT),A
	CALL	ADVANC
	LD	A,(PFRM)
	OR	A
	RET	NZ		;the same row
	LD	A,(EPAT)	;the markers: the row left, the row now
	LD	B,A
	LD	A,(OPAT)
	CP	B
	LD	A,(OROW)
	CALL	Z,DRAWRV
	LD	A,(EPAT)
	LD	B,A
	LD	A,(PPAT)
	CP	B
	LD	A,(PROW)
	CALL	Z,DRAWRV
	LD	A,(PORD)
	LD	B,A
	LD	A,(OORD)
	CP	B
	RET	Z
	JP	DRAWORD

;	A row's notes start: PPAT's row PROW.
ROWST:	LD	A,(PPAT)
	LD	B,A
	LD	A,(PROW)
	LD	C,A
	XOR	A
	CALL	CELL
	LD	IX,CHAN
	LD	B,3
RS1:	LD	A,(HL)
	INC	HL
	LD	C,(HL)
	INC	HL
	OR	A
	JR	Z,RS3		;empty: the note goes on
	CP	0FFH
	JR	NZ,RS2
	LD	(IX+3),0FFH	;off
	JR	RS3
RS2:	PUSH	HL
	PUSH	BC
	CALL	PERIOD
	LD	(IX+0),L
	LD	(IX+1),H
	POP	BC
	POP	HL
	LD	A,C
	AND	NINST-1
	LD	(IX+2),A
	LD	(IX+3),0
RS3:	LD	DE,4
	ADD	IX,DE
	DJNZ	RS1
	RET

;	The next frame: the next row after SSPEED frames, the next pattern
;	after the last row (the song's next order, or the pattern again);
;	past the song's end PENDF is set and it starts again.
ADVANC:	LD	A,(SSPEED)
	LD	B,A
	LD	A,(PFRM)
	INC	A
	CP	B
	JR	C,AD1
	XOR	A
	LD	(PFRM),A
	LD	A,(PROW)
	INC	A
	CP	NROWS
	JR	C,AD2
	LD	A,(PMODE)
	CP	1
	JR	NZ,AD3
	LD	A,(SOLEN)
	LD	B,A
	LD	A,(PORD)
	INC	A
	CP	B
	JR	C,AD4
	LD	A,1
	LD	(PENDF),A
	XOR	A
AD4:	LD	(PORD),A
	CALL	ORDPAT
	LD	(PPAT),A
AD3:	XOR	A
AD2:	LD	(PROW),A
	JP	ROWST
AD1:	LD	(PFRM),A
	RET

;	One frame of the chip's registers from the channels, the changed
;	ones written (OUTMOD 0: to the chip; 1: into the PSG file, and its
;	frame end).
FRAME:	LD	A,3FH
	LD	(MIXV),A
	LD	IX,CHAN
	LD	B,0
FR1:	LD	A,(IX+3)
	CP	0FFH
	JR	Z,FR6		;silent
	LD	A,(IX+2)
	PUSH	BC
	CALL	INSA
	POP	BC
	LD	A,(IX+3)	;the step, the last held
	CP	16
	JR	C,FR2
	LD	A,15
FR2:	LD	E,A
	LD	D,0
	PUSH	HL
	ADD	HL,DE
	LD	A,(HL)
	AND	0FH
	LD	(VOLT),A
	POP	HL
	LD	A,(IX+3)
	CP	16
	JR	NC,FR3
	INC	A
	LD	(IX+3),A
FR3:	LD	DE,16
	ADD	HL,DE
	LD	D,(HL)		;the mode
	INC	HL
	LD	E,(HL)		;the noise period
	LD	A,D
	AND	3
	JR	NZ,FR4
	LD	(VOLT),A	;neither: silent
FR4:	BIT	0,D
	JR	Z,FR5
	LD	A,B
	CALL	CLRMIX
FR5:	BIT	1,D
	JR	Z,FR7
	LD	A,E
	LD	(NOISEV),A
	LD	A,B
	ADD	A,3
	CALL	CLRMIX
FR7:	LD	HL,REGS		;the period, the volume
	LD	A,B
	ADD	A,A
	LD	E,A
	LD	D,0
	ADD	HL,DE
	LD	A,(IX+0)
	LD	(HL),A
	INC	HL
	LD	A,(IX+1)
	LD	(HL),A
	LD	A,(VOLT)
	JR	FR8
FR6:	XOR	A
FR8:	LD	HL,REGS+8
	LD	E,B
	LD	D,0
	ADD	HL,DE
	LD	(HL),A
	LD	DE,4
	ADD	IX,DE
	INC	B
	LD	A,B
	CP	3
	JR	C,FR1
	LD	A,(MIXV)
	LD	(REGS+7),A
	LD	A,(NOISEV)
	LD	(REGS+6),A
	LD	HL,REGS		;the changed ones out
	LD	DE,SHAD
	LD	B,0
FR9:	LD	A,(DE)
	CP	(HL)
	JR	Z,FR10
	LD	A,(HL)
	LD	(DE),A
	PUSH	HL
	PUSH	DE
	PUSH	BC
	LD	E,A
	LD	A,B
	CALL	OUTREG
	POP	BC
	POP	DE
	POP	HL
FR10:	INC	HL
	INC	DE
	INC	B
	LD	A,B
	CP	14
	JR	C,FR9
	LD	A,(OUTMOD)
	OR	A
	RET	Z
	LD	A,0FFH
	JP	PUTPSG

;	The mixer's bit A cleared (that sound on).
CLRMIX:	PUSH	BC
	LD	C,1
	OR	A
	JR	Z,CM2
CM1:	SLA	C
	DEC	A
	JR	NZ,CM1
CM2:	LD	A,C
	CPL
	LD	C,A
	LD	A,(MIXV)
	AND	C
	LD	(MIXV),A
	POP	BC
	RET

;	Register A = E, to the chip or the PSG file.
OUTREG:	LD	B,A
	LD	A,(OUTMOD)
	OR	A
	LD	A,B
	JR	NZ,OR1
	LD	A,(HASAY)
	OR	A
	RET	Z
	LD	A,B
	JP	AYW
OR1:	CALL	PUTPSG
	LD	A,E
	JP	PUTPSG

;	HL = note A's tone period (octave 1 doubles octave 2's).
PERIOD:	LD	B,0
PE1:	CP	12
	JR	C,PE2
	SUB	12
	INC	B
	JR	PE1
PE2:	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,OCT2
	ADD	HL,DE
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	EX	DE,HL
	LD	A,B
	CP	2
	JR	NC,PE4
	LD	A,2
	SUB	B
PE3:	ADD	HL,HL
	DEC	A
	JR	NZ,PE3
	RET
PE4:	SUB	2
PE5:	OR	A
	RET	Z
	SRL	H
	RR	L
	DEC	A
	JR	PE5

;	Note A on channel A for a moment with the instrument at hand, when
;	nothing plays: the player on the pattern's row alone.
PREVIEW: LD	B,A
	LD	A,(PMODE)
	OR	A
	RET	NZ
	LD	A,(HASTK)
	OR	A
	RET	Z
	LD	A,(HASAY)
	OR	A
	RET	Z
	LD	A,B
	CP	0FFH
	RET	Z
	PUSH	AF
	CALL	PINIT
	POP	AF
	CALL	PERIOD
	LD	(CHAN),HL
	LD	A,(PVINS)
	LD	(CHAN+2),A
	XOR	A
	LD	(CHAN+3),A
	LD	(OUTMOD),A
	LD	B,25		;half a second
PV1:	PUSH	BC
	CALL	FRAME
	CALL	TICK
	LD	DE,20
	ADD	HL,DE
	LD	(PDUE),HL
PV2:	CALL	TICK
	LD	DE,(PDUE)
	OR	A
	SBC	HL,DE
	BIT	7,H
	JR	NZ,PV2
	POP	BC
	DJNZ	PV1
	LD	A,(CURINS)
	LD	(PVINS),A
	JP	QUIET

;	---- files ----

;	A blank song: one pattern in the order, speed 6, the instruments
;	as in DEFINS.
NEWSONG: LD	HL,SONG
	LD	DE,SONG+1
	LD	BC,SONGSZ-1
	LD	(HL),0
	LDIR
	LD	HL,DEFHDR
	LD	DE,SONG
	LD	BC,7
	LDIR
	LD	HL,DEFINS
	LD	DE,SINST
	LD	BC,NINST*ISIZE
	LDIR
	RET

;	NAME.AYT into SONG, if there is one ("New song" else).
LOAD:	LD	HL,STYPE
	CALL	SETFCB
	LD	DE,WFCB
	LD	C,15
	CALL	BDOS
	INC	A
	LD	HL,SNEW
	JP	Z,MSG
	LD	HL,SONG
	LD	B,SONGSZ/128
LD1:	PUSH	BC
	PUSH	HL
	EX	DE,HL
	LD	C,26
	CALL	BDOS
	LD	DE,WFCB
	LD	C,20
	CALL	BDOS
	POP	HL
	POP	BC
	OR	A
	JR	NZ,LD2		;its end
	LD	DE,128
	ADD	HL,DE
	DJNZ	LD1
LD2:	LD	HL,SONG		;one of ours?
	LD	DE,DEFHDR
	LD	B,4
LD3:	LD	A,(DE)
	CP	(HL)
	JR	NZ,LD4
	INC	HL
	INC	DE
	DJNZ	LD3
	LD	HL,SLOADED
	JP	MSG
LD4:	CALL	NEWSONG
	LD	HL,SNOTAY
	JP	MSG

;	SONG into NAME.AYT.
SAVE:	LD	HL,STYPE
	CALL	FMAKE
	JR	NZ,SV2
	LD	HL,SONG
	LD	B,SONGSZ/128
SV1:	PUSH	BC
	PUSH	HL
	EX	DE,HL
	LD	C,26
	CALL	BDOS
	LD	DE,WFCB
	LD	C,21
	CALL	BDOS
	POP	HL
	POP	BC
	OR	A
	JR	NZ,SV2
	LD	DE,128
	ADD	HL,DE
	DJNZ	SV1
	CALL	FCLOSE
	LD	HL,SSAVED
	CALL	MSG
	JP	PNAMEA
SV2:	LD	HL,SFULL
	JP	MSG

;	The song, from its first order to its end, into NAME.PSG.
EXPORT:	CALL	STOP
	LD	HL,SPSGT
	CALL	FMAKE
	JP	NZ,SV2
	XOR	A
	LD	(EPOS),A
	LD	(EERR),A
	LD	A,1
	LD	(OUTMOD),A
	LD	HL,PSGHDR	;the header
	LD	B,16
EX1:	LD	A,(HL)
	PUSH	HL
	PUSH	BC
	CALL	PUTPSG
	POP	BC
	POP	HL
	INC	HL
	DJNZ	EX1
	CALL	PINIT
	LD	A,1
	LD	(PMODE),A
	CALL	ORDPAT
	LD	(PPAT),A
	CALL	ROWST
EX2:	CALL	FRAME
	CALL	ADVANC
	LD	A,(EERR)
	OR	A
	JR	NZ,EX3
	LD	A,(PENDF)
	OR	A
	JR	Z,EX2
EX3:	LD	HL,CHAN+3	;then a silent frame and the end
	LD	B,3
EX4:	LD	(HL),0FFH
	INC	HL
	INC	HL
	INC	HL
	INC	HL
	DJNZ	EX4
	CALL	FRAME
	LD	A,0FDH
	CALL	PUTPSG
EX5:	LD	A,(EPOS)	;the last record, padded
	OR	A
	JR	Z,EX6
	LD	A,1AH
	CALL	PUTPSG
	JR	EX5
EX6:	XOR	A
	LD	(PMODE),A
	LD	(OUTMOD),A
	CALL	QUIET
	LD	A,(EERR)
	OR	A
	JP	NZ,SV2
	CALL	FCLOSE
	LD	HL,SEXPORT
	CALL	MSG
	LD	HL,SPSGT
	JP	PNAME

;	A into the PSG file (a record written each 128).
PUTPSG:	PUSH	HL
	PUSH	DE
	PUSH	BC
	LD	C,A
	LD	A,(EPOS)
	LD	E,A
	LD	D,0
	LD	HL,EBUF
	ADD	HL,DE
	LD	(HL),C
	INC	A
	LD	(EPOS),A
	CP	128
	JR	C,PP1
	XOR	A
	LD	(EPOS),A
	LD	DE,EBUF
	LD	C,26
	CALL	BDOS
	LD	DE,WFCB
	LD	C,21
	CALL	BDOS
	OR	A
	JR	Z,PP1
	LD	(EERR),A
PP1:	POP	BC
	POP	DE
	POP	HL
	RET

;	WFCB = the drive and name with type HL, the rest 0.
SETFCB:	PUSH	HL
	LD	HL,WFCB
	LD	B,36
SF1:	LD	(HL),0
	INC	HL
	DJNZ	SF1
	LD	A,(FDRV)
	LD	(WFCB),A
	LD	HL,FNAME
	LD	DE,WFCB+1
	LD	BC,8
	LDIR
	POP	HL
	LD	BC,3
	LDIR
	RET

;	A new, empty file of type HL (an old one deleted); NZ if the
;	directory is full.
FMAKE:	CALL	SETFCB
	LD	DE,WFCB
	LD	C,19
	CALL	BDOS
	LD	DE,WFCB+12	;the rest 0 again for the new file
	XOR	A
	LD	B,24
FM1:	LD	(DE),A
	INC	DE
	DJNZ	FM1
	LD	DE,WFCB
	LD	C,22
	CALL	BDOS
	INC	A
	JR	Z,FM2
	XOR	A
	RET
FM2:	OR	1
	RET

FCLOSE:	LD	DE,WFCB
	LD	C,16
	JP	BDOS

;	---- the screen ----

DRAWALL: LD	HL,SCLS
	CALL	PUTS
	CALL	DRAWHDR
	LD	HL,HEADS
DA1:	LD	A,(HL)		;the column heads: row, column, text
	OR	A
	JR	Z,DA2
	LD	D,A
	INC	HL
	LD	E,(HL)
	INC	HL
	PUSH	HL
	EX	DE,HL
	CALL	GOTO
	POP	HL
	CALL	PUTS
	INC	HL
	JR	DA1
DA2:	CALL	DRAWPAN
	LD	HL,(MSGP)	;the message so far
	LD	A,H
	OR	L
	RET	Z
	JP	MSG

DRAWPAN: CALL	DRAWPAT
	CALL	DRAWORD
	JP	DRAWINS

;	The top line: the name, the pattern, speed, octave, instrument,
;	what plays.
DRAWHDR: LD	HL,1*256+1
	CALL	GOTO
	LD	HL,STITLE
	CALL	PUTS
	CALL	PNAMEA
	LD	HL,SPATT
	CALL	PUTS
	LD	A,(EPAT)
	CALL	PHEX1
	LD	HL,SSPD
	CALL	PUTS
	LD	A,(SSPEED)
	CALL	PDEC2
	LD	HL,SOCT
	CALL	PUTS
	LD	A,(OCTAVE)
	ADD	A,'0'
	CALL	PUTC
	LD	HL,SINS
	CALL	PUTS
	LD	A,(CURINS)
	ADD	A,'0'
	CALL	PUTC
	LD	A,' '
	CALL	PUTC
	CALL	PUTC
	LD	A,(PMODE)
	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,SMODES
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	CALL	PUTS
	LD	HL,SEOL
	JP	PUTS

;	The pattern rows on screen.
DRAWPAT: LD	A,(PTOP)
	LD	B,VROWS
DP1:	PUSH	BC
	PUSH	AF
	CALL	DRAWROW
	POP	AF
	POP	BC
	INC	A
	DJNZ	DP1
	RET

;	Row A of the pattern on screen, if it is on screen (DRAWRV: the
;	same; for the player's markers).
DRAWRV:
DRAWROW: LD	(DRROW),A
	LD	B,A
	LD	A,(PTOP)
	LD	C,A
	LD	A,B
	SUB	C
	RET	C
	CP	VROWS
	RET	NC
	ADD	A,TOP
	LD	H,A
	LD	L,2
	CALL	GOTO
	LD	A,(PMODE)	;the player's marker
	OR	A
	JR	Z,DR1
	LD	A,(EPAT)
	LD	B,A
	LD	A,(PPAT)
	CP	B
	JR	NZ,DR1
	LD	A,(DRROW)
	LD	B,A
	LD	A,(PROW)
	CP	B
	LD	A,'>'
	JR	Z,DR2
DR1:	LD	A,' '
DR2:	CALL	PUTC
	LD	A,(DRROW)
	CALL	PDEC2
	LD	C,0		;the channels
DR3:	PUSH	BC
	LD	HL,SSP2
	CALL	PUTS
	LD	A,(EPAT)
	LD	B,A
	LD	A,(DRROW)
	LD	E,C
	LD	C,A
	LD	A,E
	CALL	CELL
	LD	D,(HL)		;the note
	INC	HL
	LD	E,(HL)		;its instrument
	POP	BC
	PUSH	BC
	PUSH	DE
	LD	A,C		;the cursor's?
	ADD	A,A
	CALL	ATCUR
	CALL	Z,REVON
	POP	DE
	PUSH	DE
	LD	A,D
	CALL	PNOTE
	CALL	REVOFF
	LD	A,' '
	CALL	PUTC
	POP	DE
	POP	BC
	PUSH	BC
	PUSH	DE
	LD	A,C
	ADD	A,A
	INC	A
	CALL	ATCUR
	CALL	Z,REVON
	POP	DE
	LD	A,D		;the instrument: none without a note
	OR	A
	JR	Z,DR5
	INC	A
	JR	Z,DR5
	LD	A,E
	ADD	A,'0'
	JR	DR4
DR5:	LD	A,'.'
DR4:	CALL	PUTC
	CALL	REVOFF
	POP	BC
	INC	C
	LD	A,C
	CP	3
	JR	C,DR3
	RET

;	Z if field A of row DRROW has the cursor (and the pattern has the
;	focus).
ATCUR:	LD	B,A
	LD	A,(FOCUS)
	OR	A
	RET	NZ
	LD	A,(CPOS)
	CP	B
	RET	NZ
	LD	A,(CROW)
	LD	B,A
	LD	A,(DRROW)
	CP	B
	RET

;	Note A: C-4, C#4, ... (... empty, === off).
PNOTE:	OR	A
	LD	HL,SEMPTY
	JP	Z,PUTS
	CP	0FFH
	LD	HL,SOFF
	JP	Z,PUTS
	LD	B,0
PN1:	CP	12
	JR	C,PN2
	SUB	12
	INC	B
	JR	PN1
PN2:	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,NAMES
	ADD	HL,DE
	LD	A,(HL)
	CALL	PUTC
	INC	HL
	LD	A,(HL)
	CALL	PUTC
	LD	A,B
	ADD	A,'0'
	JP	PUTC

;	The order list: 16 entries from OTOP.
DRAWORD: LD	A,(OTOP)
	LD	C,A
	LD	B,0
DO1:	PUSH	BC
	LD	A,B
	ADD	A,TOP
	LD	H,A
	LD	L,38
	CALL	GOTO
	POP	BC
	PUSH	BC
	LD	A,(SOLEN)
	LD	D,A
	LD	A,C
	CP	D
	JR	NC,DO4		;past its end
	LD	D,A
	LD	A,(PMODE)	;the player's marker
	CP	1
	JR	NZ,DO2
	LD	A,(PORD)
	CP	D
	LD	A,'>'
	JR	Z,DO3
DO2:	LD	A,' '
DO3:	CALL	PUTC
	LD	A,D
	CALL	PDEC2
	LD	A,' '
	CALL	PUTC
	LD	A,(FOCUS)
	CP	1
	JR	NZ,DO5
	LD	A,(EORDP)
	CP	D
	CALL	Z,REVON
DO5:	LD	A,D
	CALL	ORDAD
	LD	A,(HL)
	CALL	PHEX1
	CALL	REVOFF
	JR	DO6
DO4:	LD	HL,SBL5
	CALL	PUTS
DO6:	POP	BC
	INC	C
	INC	B
	LD	A,B
	CP	VROWS
	JR	C,DO1
	RET

;	The instruments: volumes, mode, noise.
DRAWINS: XOR	A
	LD	(DINS),A
DI1:	LD	A,(DINS)
	ADD	A,TOP
	LD	H,A
	LD	L,46
	CALL	GOTO
	LD	A,(CURINS)
	LD	B,A
	LD	A,(DINS)
	CP	B
	LD	A,'>'
	JR	Z,DI2
	LD	A,' '
DI2:	CALL	PUTC
	LD	A,(DINS)
	ADD	A,'0'
	CALL	PUTC
	LD	A,' '
	CALL	PUTC
	LD	A,(DINS)
	CALL	INSA
	LD	(DPTR),HL
	XOR	A
	LD	(DFLD),A
DI3:	LD	A,(DFLD)	;a space before the mode and the noise
	CP	16
	JR	C,DI4
	LD	A,' '
	CALL	PUTC
DI4:	CALL	DICUR
	CALL	Z,REVON
	LD	HL,(DPTR)
	LD	A,(HL)
	LD	B,A
	LD	A,(DFLD)
	CP	16
	LD	A,B
	JR	C,DI5
	JR	Z,DI6
	PUSH	AF		;the noise period: two digits
	RRCA
	RRCA
	RRCA
	RRCA
	CALL	PHEX1
	POP	AF
DI5:	CALL	PHEX1		;a volume
	JR	DI7
DI6:	AND	3		;the mode
	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,MODES
	ADD	HL,DE
	LD	A,(HL)
	CALL	PUTC
	INC	HL
	LD	A,(HL)
	CALL	PUTC
DI7:	CALL	REVOFF
	LD	HL,(DPTR)
	INC	HL
	LD	(DPTR),HL
	LD	A,(DFLD)
	INC	A
	LD	(DFLD),A
	CP	ISIZE
	JR	C,DI3
	LD	A,(DINS)
	INC	A
	LD	(DINS),A
	CP	NINST
	JP	C,DI1
	RET

;	Z if field DFLD of instrument DINS has the cursor (and the
;	instruments have the focus).
DICUR:	LD	A,(FOCUS)
	CP	2
	RET	NZ
	LD	A,(CURINS)
	LD	B,A
	LD	A,(DINS)
	CP	B
	RET	NZ
	LD	A,(IFIELD)
	LD	B,A
	LD	A,(DFLD)
	CP	B
	RET

;	Text HL on the message line (kept for a redraw).
MSG:	LD	(MSGP),HL
	PUSH	HL
	LD	HL,MSGROW*256+2
	CALL	GOTO
	LD	HL,SEOL
	CALL	PUTS
	POP	HL
	JP	PUTS

QUIT:	LD	HL,SQUIT
	CALL	MSG
QU1:	CALL	PLAYTK
	CALL	RDKEY
	OR	A
	JR	Z,QU1
	AND	5FH
	CP	'Y'
	JR	Z,QU2
	LD	HL,SBLANK
	JP	MSG
QU2:	CALL	QUIET
	LD	HL,SCLS
	CALL	PUTS
	JP	0

;	The cursor to row H, column L.
GOTO:	PUSH	HL
	LD	A,ESC
	CALL	PUTC
	LD	A,'['
	CALL	PUTC
	POP	HL
	PUSH	HL
	LD	A,H
	CALL	PDEC
	LD	A,';'
	CALL	PUTC
	POP	HL
	LD	A,L
	CALL	PDEC
	LD	A,'H'
	JP	PUTC

REVON:	PUSH	HL
	LD	HL,SREV
	CALL	PUTS
	POP	HL
	RET

REVOFF:	PUSH	HL
	LD	HL,SNORM
	CALL	PUTS
	POP	HL
	RET

;	The file's name with type AYT (PNAME: with type HL).
PNAMEA:	LD	HL,STYPE
PNAME:	PUSH	HL
	LD	A,(FDRV)
	OR	A
	JR	Z,PM1
	ADD	A,'@'
	CALL	PUTC
	LD	A,':'
	CALL	PUTC
PM1:	LD	HL,FNAME
	LD	B,8
PM2:	LD	A,(HL)
	CP	' '
	CALL	NZ,PUTC
	INC	HL
	DJNZ	PM2
	LD	A,'.'
	CALL	PUTC
	POP	HL
	LD	B,3
PM3:	LD	A,(HL)
	CALL	PUTC
	INC	HL
	DJNZ	PM3
	RET

;	A (0-99) as two digits.
PDEC2:	LD	B,'0'-1
P21:	INC	B
	SUB	10
	JR	NC,P21
	ADD	A,10+'0'
	PUSH	AF
	LD	A,B
	CALL	PUTC
	POP	AF
	JP	PUTC

;	A (0-99) in decimal, no leading zero.
PDEC:	CP	10
	JR	C,PD2
	LD	B,'0'
PD1:	INC	B
	SUB	10
	CP	10
	JR	NC,PD1
	PUSH	AF
	LD	A,B
	CALL	PUTC
	POP	AF
PD2:	ADD	A,'0'
	JP	PUTC

;	A's low nibble as a hex digit.
PHEX1:	AND	0FH
	ADD	A,'0'
	CP	'9'+1
	JR	C,PH1
	ADD	A,7
PH1:	JP	PUTC

;	A to the console through BDOS 6; keeps AF, BC, DE, HL.
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

;	Text HL (to its 0), HL left on the 0.
PUTS:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	HL
	JR	PUTS

;	---- the chip ----

PROBE:	LD	A,1
	LD	E,0FFH
	CALL	AYW
	LD	A,1
	CALL	AYR
	CP	0FH
	RET	NZ
	LD	A,2
	LD	E,5AH
	CALL	AYW
	LD	A,2
	CALL	AYR
	CP	5AH
	RET

;	Register A = E. Keeps AF, BC, DE, HL.
AYW:	OUT	(AYSEL),A
	PUSH	AF
	LD	A,E
	OUT	(AYDAT),A
	POP	AF
	RET

AYR:	OUT	(AYSEL),A
	IN	A,(AYDAT)
	RET

;	The chip silent (without one: nothing).
QUIET:	LD	A,(HASAY)
	OR	A
	RET	Z
	LD	B,0
Q1:	LD	A,B
	LD	E,0
	CALL	AYW
	INC	B
	LD	A,B
	CP	14
	JR	C,Q1
	LD	A,7
	LD	E,3FH
	JP	AYW

TICK:	IN	A,(TICKL)
	LD	L,A
	IN	A,(TICKH)
	LD	H,A
	RET

;	Z if the tick moves within 4 x 65536 reads.
TICKOK:	CALL	TICK
	LD	(PDUE),HL
	LD	B,4
TK0:	LD	DE,0
TK1:	PUSH	DE
	CALL	TICK
	LD	DE,(PDUE)
	OR	A
	SBC	HL,DE
	POP	DE
	JR	NZ,TK2
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,TK1
	DJNZ	TK0
	OR	1
	RET
TK2:	XOR	A
	RET

;	---- data ----

;	The note keys, a piano on the keyboard: Z to M the octave, Q to U
;	the next (black keys on the row above each).
KEYTAB:	DB	'zsxdcvgbhnjm'
	DB	'q2w3er5t6y7u'
ARROWS:	DB	KUP,KDOWN,KRIGHT,KLEFT
;	C2 to B2's periods (115200 / f); other octaves halve or double.
OCT2:	DW	1761,1662,1569,1481,1398,1319,1245,1175,1110,1047,988,933
NAMES:	DB	'C-C#D-D#E-F-F#G-G#A-A#B-'
MODES:	DB	'--T N TN'
DEFNAM:	DB	'SONG    '
STYPE:	DB	'AYT'
SPSGT:	DB	'PSG'
DEFHDR:	DB	'AYT1',6,1,0	;magic, speed, one order: pattern 0
PSGHDR:	DB	'PSG',1AH,0,0,0,0,0,0,0,0,0,0,0,0
;	The instruments: 16 volumes, mode (1 tone, 2 noise, 3 both), noise.
DEFINS:	DB	15,14,13,12,11,11,10,10,10,9,9,9,8,8,8,8,1,0	;0 lead
	DB	13,12,11,10,9,9,8,8,7,7,6,6,5,5,4,4,1,0		;1 bass
	DB	15,12,9,6,4,2,1,0,0,0,0,0,0,0,0,0,1,0		;2 pluck
	DB	6,8,10,11,11,11,11,11,11,11,11,11,11,11,11,11,1,0 ;3 pad
	DB	10,6,3,1,0,0,0,0,0,0,0,0,0,0,0,0,2,1		;4 hi-hat
	DB	14,11,8,6,4,2,0,0,0,0,0,0,0,0,0,0,2,8		;5 snare
	DB	15,12,8,4,2,0,0,0,0,0,0,0,0,0,0,0,3,31		;6 kick
	DB	11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,1,0 ;7 organ

;	Column heads: row, column, text.
HEADS:	DB	2,2,'Row  A      B      C',0
	DB	2,38,'Ord',0
	DB	2,46,'#  Volumes          Md Ns',0
	DB	12,44,'Keys to notes: the octave, then +1',0
	DB	13,44,'C  C# D  D# E  F  F# G  G# A  A# B',0
	DB	14,44,'Z  S  X  D  C  V  G  B  H  N  J  M',0
	DB	15,44,'Q  2  W  3  E  R  5  T  6  Y  7  U',0
	DB	22,2,'Notes: keys as in the table above right  = off  . clear  0-7 instrument',0
	DB	23,2,'[ ] octave  < > speed  ^N ^P pattern',0
	DB	24,2,'Tab panel  Space song  Return pattern  ^S save  ^E export .PSG  ^Q quit',0
	DB	21,2,'Orders: 0-F its pattern, I insert, X delete   Instruments: 0-F a value',0
	DB	0
SCLS:	DB	ESC,'[H',ESC,'[2J',0
SEOL:	DB	ESC,'[K',0
SREV:	DB	ESC,'[7m',0
SNORM:	DB	ESC,'[m',0
STITLE:	DB	' AYTRACK  ',0
SPATT:	DB	'  Pattern ',0
SSPD:	DB	'  Speed ',0
SOCT:	DB	'  Octave ',0
SINS:	DB	'  Inst ',0
SMODES:	DW	SSTOP,SPSONG,SPPAT
SSTOP:	DB	'Stopped',0
SPSONG:	DB	'Playing the song',0
SPPAT:	DB	'Playing the pattern',0
SSP2:	DB	'  ',0
SEMPTY:	DB	'...',0
SOFF:	DB	'===',0
SBL5:	DB	'     ',0
SNEW:	DB	'New song',0
SLOADED: DB	'Loaded',0
SNOTAY:	DB	'Not an AYTRACK song: a new one',0
SSAVED:	DB	'Saved ',0
SEXPORT: DB	'Exported ',0
SFULL:	DB	'Disk or directory full',0
SQUIT:	DB	'Quit (unsaved changes are lost)? Y/N',0
SBLANK:	DB	0
SNOSND:	DB	'No AY-3-8910 or no millisecond tick: playing is off',0

HASAY:	DB	0
HASTK:	DB	0
FOCUS:	DB	0		;0 the pattern, 1 the orders, 2 the instruments
CROW:	DB	0		;the cursor: row, field (channel x 2 + 1 instrument)
CPOS:	DB	0
PTOP:	DB	0		;the first row on screen
EPAT:	DB	0		;the pattern on screen
EORDP:	DB	0		;the selected order
OTOP:	DB	0
OCTAVE:	DB	4
CURINS:	DB	0
PVINS:	DB	0		;the instrument a preview uses
IFIELD:	DB	0
PMODE:	DB	0		;0 stopped, 1 the song, 2 the pattern
PORD:	DB	0		;the player: order, pattern, row, frame
PPAT:	DB	0
PROW:	DB	0
PFRM:	DB	0
PENDF:	DB	0		;past the song's end
OROW:	DB	0
OORD:	DB	0
OPAT:	DB	0
DRROW:	DB	0
NOTEV:	DB	0
DINS:	DB	0
DFLD:	DB	0
DPTR:	DW	0
OUTMOD:	DB	0
MIXV:	DB	0
NOISEV:	DB	0
VOLT:	DB	0
EPOS:	DB	0
EERR:	DB	0
FDRV:	DB	0
PDUE:	DW	0
MSGP:	DW	0
FNAME:	DS	8
CHAN:	DS	12		;per channel: period, instrument, step (FFH silent)
REGS:	DS	14
SHAD:	DS	14
WFCB:	DS	36
EBUF:	DS	128
	DS	96
STACK:
	END	START
