;	AYTEST: plays the AY-3-8910 sound chip, a part at a time.
;
;	AYTEST
;
;	Finds the chip (src/machine/machine.h, bd 1ls.18.4) on RC2014's
;	ports (D8h selects a register, D0h writes and reads it): a register
;	written must read back, with the bits it keeps. Then a menu: a scale on
;	channel A, chords on A, B and C, a noise sweep, the 16 envelope
;	shapes over a tone, a volume ramp, A, B and C in turn (to hear the
;	stereo layout), or all of them; a key stops what is playing (back to
;	the menu, the chip silenced), Q, ESC or ^C at the menu ends the
;	program. With the firmware's stereo register (16, ours; bd 1ls.18.9)
;	the menu shows the layout in use and S steps it: SET-UP's, mono,
;	ABC, ACB; the program puts back the one it found when it ends. Durations come from the firmware's
;	millisecond tick (ports 68/69), as the Z80 runs at no fixed speed;
;	without it AYTEST says so. Periods are for the 1.8432 MHz clock
;	(RC2014's): a tone of f Hz has period 115200 / f.
;	Works on CP/M 2.2 and 3 (the console through the BDOS only).
;
;	Z80 mnemonics; assembled with z80pack's z80asm by build.sh, put on
;	the CPM22, CPM31 and CPM32 disks by diskimg_gen.py.

BDOS	EQU	5
TICKL	EQU	68		;the millisecond tick: low (latches the high)
TICKH	EQU	69
NOISE	EQU	6		;the chip's registers
MIXER	EQU	7
AMPA	EQU	8
ENVF	EQU	11
ENVC	EQU	12
SHAPE	EQU	13
STEREO	EQU	16		;ours: the layout (machine.h MACHINE_AY_STEREO)
ENVBIT	EQU	10H		;an amplitude: the envelope's
CR	EQU	0DH
LF	EQU	0AH

	ORG	100H

START:	LD	SP,STACK
	CALL	PROBE
	JR	Z,FOUND
	LD	HL,SNOAY
	CALL	PUTS
	JP	0
FOUND:	LD	A,STEREO	;0FFH without it
	CALL	AYR
	CP	0FFH
	JR	Z,F1
	AND	3		;what to put back
	LD	(ST0),A
	LD	A,1
	LD	(HASST),A
F1:	CALL	TICKOK
	JR	Z,MENU
	LD	HL,SNOTICK
	CALL	PUTS
	JP	0

MENU:	CALL	QUIET
	LD	HL,STITLE
	CALL	PUTS
	LD	HL,(PORTN)
	CALL	PUTS
	LD	HL,SMENU
	CALL	PUTS
	LD	A,(HASST)
	OR	A
	JR	Z,MN1
	LD	HL,SSTER	;the layout in use, and whose
	CALL	PUTS
	CALL	INUSE
	ADD	A,A
	LD	L,A
	LD	H,0
	LD	DE,LNAMES
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	CALL	PUTS
	LD	A,STEREO
	CALL	AYR
	AND	3
	LD	HL,SSETUP
	JR	Z,MN0
	LD	HL,SHERE
MN0:	CALL	PUTS
MN1:	LD	HL,SCHOICE
	CALL	PUTS
M1:	CALL	GETKEY
	CP	'1'
	JR	C,M2
	CP	'8'
	JR	NC,M2
	CALL	PUTC		;1-7: echo it, play it
	PUSH	AF
	CALL	CRLF
	POP	AF
	SUB	'1'
	ADD	A,A
	LD	L,A
	LD	H,0
	LD	DE,ITEMS
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	LD	(ITEMSP),SP	;a key while it plays comes back to here
	CALL	JPHL
	JR	MENU
M2:	AND	5FH		;upper case (^C, ESC as they were)
	CP	'S'
	JR	Z,STER
	CP	'Q'
	JR	Z,QUIT
	CP	3
	JR	Z,QUIT
	CP	1BH
	JR	NZ,M1
QUIT:	CALL	QUIET
	LD	A,(HASST)
	OR	A
	JR	Z,QT1
	LD	A,(ST0)		;the layout as found
	LD	E,A
	LD	A,STEREO
	CALL	AYW
QT1:	CALL	CRLF
	JP	0

;	S: the next layout (SET-UP's, mono, ABC, ACB).
STER:	LD	A,(HASST)
	OR	A
	JR	Z,M1
	LD	A,STEREO
	CALL	AYR
	INC	A
	AND	3
	LD	E,A
	LD	A,STEREO
	CALL	AYW
	JP	MENU

JPHL:	JP	(HL)

ITEMS:	DW	SCALE,CHORDS,NOISES,ENVS,VOLS,PAN,ALL

ALL:	CALL	SCALE
	CALL	CHORDS
	CALL	NOISES
	CALL	ENVS
	CALL	VOLS
	JP	PAN

;	---- the parts ----

;	C4 to C5 on A, 300 ms a note.
SCALE:	LD	HL,SSCALE
	CALL	PUTS
	CALL	QUIET
	LD	A,MIXER		;tone A only
	LD	E,3EH
	CALL	AYW
	LD	A,AMPA
	LD	E,13
	CALL	AYW
	LD	HL,NOTES
	LD	B,8
SC1:	PUSH	BC
	CALL	NAME		;its name, then its period on A
	LD	B,0
	CALL	TONEHL
	PUSH	HL
	LD	HL,300
	CALL	WAIT
	POP	HL
	POP	BC
	DJNZ	SC1
	JP	CRLF

;	C, F, G, C major on A, B and C, 700 ms each.
CHORDS:	LD	HL,SCHORD
	CALL	PUTS
	CALL	QUIET
	LD	A,MIXER		;tones A, B, C
	LD	E,38H
	CALL	AYW
	LD	B,3
CH0:	LD	A,AMPA-1
	ADD	A,B
	LD	E,11
	CALL	AYW
	DJNZ	CH0
	LD	HL,CHORDT
	LD	B,4
CH1:	PUSH	BC
	CALL	NAME
	LD	B,0
CH2:	CALL	TONEHL
	INC	B
	LD	A,B
	CP	3
	JR	C,CH2
	PUSH	HL
	LD	HL,700
	CALL	WAIT
	POP	HL
	POP	BC
	DJNZ	CH1
	JP	CRLF

;	Noise on A, its period 31 down to 1, 60 ms each.
NOISES:	LD	HL,SNOISE
	CALL	PUTS
	CALL	QUIET
	LD	A,MIXER		;noise A only
	LD	E,37H
	CALL	AYW
	LD	A,AMPA
	LD	E,12
	CALL	AYW
	LD	B,31
NS1:	LD	A,NOISE
	LD	E,B
	CALL	AYW
	LD	HL,60
	CALL	WAIT
	DJNZ	NS1
	JP	CRLF

;	Each shape for a second, over C4 on A: a ramp takes 250 ms
;	(period 1800, 1.8432 MHz / (256 x 1800) = 4 a second).
ENVS:	LD	HL,SENV
	CALL	PUTS
	CALL	QUIET
	LD	A,MIXER
	LD	E,3EH
	CALL	AYW
	LD	B,0
	LD	DE,440		;C4
	CALL	TONE
	LD	A,AMPA
	LD	E,ENVBIT
	CALL	AYW
	LD	A,ENVF
	LD	E,08H		;1800 = 0708H
	CALL	AYW
	LD	A,ENVC
	LD	E,07H
	CALL	AYW
	LD	B,0
EN1:	PUSH	BC
	LD	HL,SSHAPE
	CALL	PUTS
	LD	A,B		;the number, two columns
	CP	10
	LD	C,' '
	JR	C,EN2
	SUB	10
	LD	C,'1'
EN2:	PUSH	AF
	LD	A,C
	CALL	PUTC
	POP	AF
	ADD	A,'0'
	CALL	PUTC
	LD	A,' '
	CALL	PUTC
	CALL	PUTC
	LD	A,B		;its drawing
	ADD	A,A
	LD	L,A
	LD	H,0
	LD	DE,SHAPES
	ADD	HL,DE
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	CALL	PUTS
	CALL	CRLF
	LD	A,SHAPE		;a write starts it
	LD	E,B
	CALL	AYW
	LD	HL,1000
	CALL	WAIT
	POP	BC
	INC	B
	LD	A,B
	CP	16
	JR	C,EN1
	RET

;	A4 on A, volume 0 up to 15 and back down, 150 ms a step.
VOLS:	LD	HL,SVOL
	CALL	PUTS
	CALL	QUIET
	LD	A,MIXER
	LD	E,3EH
	CALL	AYW
	LD	B,0
	LD	DE,262		;A4
	CALL	TONE
	LD	B,0
V1:	CALL	VSTEP
	INC	B
	LD	A,B
	CP	16
	JR	C,V1
	DEC	B
V2:	DEC	B
	CALL	VSTEP
	LD	A,B
	OR	A
	JR	NZ,V2
	JP	CRLF

VSTEP:	LD	A,AMPA
	LD	E,B
	CALL	AYW
	LD	HL,150
	JP	WAIT

;	A4 on A, then B, then C, a second each, with where the layout in
;	use puts it.
PAN:	LD	HL,SPAN
	CALL	PUTS
	CALL	QUIET
	CALL	INUSE		;its three places: WHERE + 6 x the layout
	LD	L,A
	ADD	A,A
	ADD	A,L
	ADD	A,A
	LD	L,A
	LD	H,0
	LD	DE,WHERE
	ADD	HL,DE
	LD	(WHEREP),HL
	LD	B,0
PN1:	PUSH	BC
	LD	A,'A'		;the channel and its place
	ADD	A,B
	CALL	PUTC
	LD	A,' '
	CALL	PUTC
	LD	HL,(WHEREP)
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	LD	(WHEREP),HL
	EX	DE,HL
	CALL	PUTS
	LD	A,' '
	CALL	PUTC
	CALL	PUTC
	LD	A,B		;the mixer: this channel's tone only
	LD	C,1
	OR	A
	JR	Z,PN3
PN2:	SLA	C
	DEC	A
	JR	NZ,PN2
PN3:	LD	A,C
	CPL
	AND	3FH
	LD	E,A
	LD	A,MIXER
	CALL	AYW
	LD	DE,262		;A4
	CALL	TONE
	LD	A,AMPA
	ADD	A,B
	LD	E,13
	CALL	AYW
	LD	HL,1000
	CALL	WAIT
	LD	A,AMPA
	ADD	A,B
	LD	E,0
	CALL	AYW
	POP	BC
	INC	B
	LD	A,B
	CP	3
	JR	C,PN1
	JP	CRLF

;	A = the layout in use: 0 mono, 1 ABC, 2 ACB (mono without the
;	stereo register). Keeps BC, DE, HL.
INUSE:	LD	A,(HASST)
	OR	A
	RET	Z
	LD	A,STEREO
	CALL	AYR
	RRCA
	RRCA
	RRCA
	RRCA
	AND	3
	RET	Z		;not expected: as mono
	DEC	A
	RET

;	---- the chip ----

;	Find it on RC2014's ports. Z: found (SELP, DATP, RDP, PORTN set;
;	another port map would be a second table for TRY).
PROBE:	LD	HL,PRC
TRY:	LD	DE,SELP		;the three ports and the name's address
	LD	BC,5
	LDIR
	LD	A,1		;register 1 keeps 4 bits
	LD	E,0FFH
	CALL	AYW
	LD	A,1
	CALL	AYR
	CP	0FH
	RET	NZ
	LD	A,2		;register 2 all 8
	LD	E,5AH
	CALL	AYW
	LD	A,2
	CALL	AYR
	CP	5AH
	RET

;	Register A = E. Keeps AF, BC, DE, HL.
AYW:	PUSH	BC
	PUSH	AF
	LD	A,(SELP)
	LD	C,A
	POP	AF
	OUT	(C),A
	PUSH	AF
	LD	A,(DATP)
	LD	C,A
	OUT	(C),E
	POP	AF
	POP	BC
	RET

;	A = register A. Keeps BC, DE, HL.
AYR:	PUSH	BC
	PUSH	AF
	LD	A,(SELP)
	LD	C,A
	POP	AF
	OUT	(C),A
	LD	A,(RDP)
	LD	C,A
	IN	A,(C)
	POP	BC
	RET

;	Channel B's tone period DE. Keeps BC, HL.
TONE:	LD	A,B
	ADD	A,A
	CALL	AYW
	INC	A
	LD	E,D
	JP	AYW

;	Channel B's tone period from (HL), HL past it. Keeps BC.
TONEHL:	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	JR	TONE

;	Everything off: periods and volumes 0, the mixer all off.
QUIET:	LD	B,0
Q1:	LD	A,B
	LD	E,0
	CALL	AYW
	INC	B
	LD	A,B
	CP	SHAPE+1
	JR	C,Q1
	LD	A,MIXER
	LD	E,3FH
	JP	AYW

;	---- time ----

;	HL = the millisecond tick.
TICK:	IN	A,(TICKL)
	LD	L,A
	IN	A,(TICKH)
	LD	H,A
	RET

;	Z if the tick moves within 4 x 65536 reads.
TICKOK:	CALL	TICK
	LD	(T0),HL
	LD	B,4
TK0:	LD	DE,0
TK1:	PUSH	DE
	CALL	TICK
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	POP	DE
	JR	NZ,TK2		;it moved
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,TK1
	DJNZ	TK0
	OR	1		;never moved: NZ
	RET
TK2:	XOR	A
	RET

;	HL milliseconds; a key meanwhile stops the part (back to the menu).
;	Keeps BC.
WAIT:	PUSH	BC
	LD	(WDUR),HL
	CALL	TICK
	LD	(T0),HL
W1:	CALL	KEYHIT
	CALL	TICK
	LD	DE,(T0)
	OR	A
	SBC	HL,DE		;the time gone
	LD	DE,(WDUR)
	OR	A
	SBC	HL,DE
	JR	C,W1
	POP	BC
	RET

;	A key: stop, back to the menu (from ITEMSP's level).
KEYHIT:	LD	C,6		;direct console input: 0 while none
	LD	E,0FFH
	CALL	BDOS
	OR	A
	RET	Z
	LD	SP,(ITEMSP)
	LD	HL,SSTOP
	CALL	PUTS
	JP	MENU

;	---- the console ----

;	A key (waits); A.
GETKEY:	LD	C,6
	LD	E,0FFH
	CALL	BDOS
	OR	A
	JR	Z,GETKEY
	RET

;	The name at (HL) and a space, HL past it. Keeps BC.
NAME:	LD	A,(HL)
	INC	HL
	CALL	PUTC
	LD	A,' '
	JP	PUTC

;	A to the console; keeps AF, BC, DE, HL. Through direct console I/O
;	(6), as the keys are read: CP/M 2.2's console output (2) takes a key
;	typed meanwhile into a buffer of its own, which 6 never sees, so a
;	key that came while a line was going out did not stop the part.
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
	JP	PUTC

;	---- data ----

PRC:	DB	0D8H,0D0H,0D0H	;select, write, read
	DW	SRC

;	Name, period (115200 / f).
NOTES:	DB	'C'
	DW	440
	DB	'D'
	DW	392
	DB	'E'
	DW	349
	DB	'F'
	DW	330
	DB	'G'
	DW	294
	DB	'A'
	DW	262
	DB	'B'
	DW	233
	DB	'C'
	DW	220
;	Name, periods on A, B, C.
CHORDT:	DB	'C'
	DW	440,349,294	;C4 E4 G4
	DB	'F'
	DW	330,262,220	;F4 A4 C5
	DB	'G'
	DW	294,233,196	;G4 B4 D5
	DB	'C'
	DW	220,175,147	;C5 E5 G5

SHAPES:	DW	SDOWN,SDOWN,SDOWN,SDOWN,SUP,SUP,SUP,SUP
	DW	SSAWD,SDOWN,STRID,SDOWNH,SSAWU,SUPH,STRIU,SUP
SDOWN:	DB	'\_______',0
SUP:	DB	'/|______',0
SSAWD:	DB	'\|\|\|\|',0
STRID:	DB	'\/\/\/\/',0
SDOWNH:	DB	'\|^^^^^^',0
SSAWU:	DB	'/|/|/|/|',0
SUPH:	DB	'/^^^^^^^',0
STRIU:	DB	'/\/\/\/\',0

STITLE:	DB	CR,LF,'AY-3-8910 test: the chip on ',0
SRC:	DB	'ports D8h/D0h (RC2014)',0
SMENU:	DB	CR,LF
	DB	'  1 Scale on A          2 Chords on A, B, C',CR,LF
	DB	'  3 Noise sweep         4 Envelope shapes',CR,LF
	DB	'  5 Volume ramp         6 A, B, C in turn (stereo)',CR,LF
	DB	'  7 All of them         Q Quit (a key stops what is playing)',CR,LF,0
SSTER:	DB	'  S Stereo: ',0
SSETUP:	DB	' (SET-UP',27H,'s)',CR,LF,0
SHERE:	DB	' (picked here)',CR,LF,0
SCHOICE: DB	'Choice: ',0
LNAMES:	DW	SMONO,SABC,SACB
SMONO:	DB	'mono',0
SABC:	DB	'ABC (A left, B center, C right)',0
SACB:	DB	'ACB (A left, C center, B right)',0
;	Where A, B and C are heard, per layout.
WHERE:	DW	SBOTH,SBOTH,SBOTH,SLEFT,SCENT,SRIGHT,SLEFT,SRIGHT,SCENT
SBOTH:	DB	'both',0
SLEFT:	DB	'left',0
SCENT:	DB	'center',0
SRIGHT:	DB	'right',0
SPAN:	DB	'A, B, C in turn: ',0
SSCALE:	DB	'Scale on A: ',0
SCHORD:	DB	'Chords on A, B, C: ',0
SNOISE:	DB	'Noise on A, period 31 to 1',0
SENV:	DB	'Envelope shapes on A over C4:',CR,LF,0
SSHAPE:	DB	'  Shape ',0
SVOL:	DB	'Volume on A over A4, 0 to 15 and back',0
SSTOP:	DB	' -- stopped',CR,LF,0
SNOAY:	DB	'No AY-3-8910 on ports D8h/D0h.',CR,LF,0
SNOTICK: DB	'No millisecond tick on ports 68/69: newer DVI Feather',CR,LF
	DB	'Terminal firmware has it.',CR,LF,0

HASST:	DB	0		;1: the stereo register is there
ST0:	DB	0		;its layout when AYTEST started

SELP:	DS	1		;the chip's ports: select, write, read
DATP:	DS	1
RDP:	DS	1
PORTN:	DS	2		;their name
ITEMSP:	DS	2		;the menu's stack, for a key's stop
T0:	DS	2
WDUR:	DS	2
WHEREP:	DS	2
	DS	64
STACK:
	END	START
