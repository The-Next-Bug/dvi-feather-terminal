;	AYPLAY: plays a PSG file on the AY-3-8910 sound chip.
;
;	AYPLAY [d:]NAME[.PSG] [MONO|ABC|ACB] [LOOP]
;	AYPLAY			(lists the .PSG files on the current drive)
;
;	PSG (bd 1ls.18.10): "PSG" 1AH and 12 more header bytes, then the
;	chip's register writes frame by frame: a register (0-15) and its
;	value, FFH ending a frame (1/50 s), FEH n waiting n x 4 frames, FDH
;	ending the tune. The file is read a record at a time while it plays,
;	so it may be any size. Frames are timed by the firmware's
;	millisecond tick (ports 68/69), as the Z80 runs at no fixed speed.
;	MONO, ABC or ACB picks the stereo layout through the firmware's
;	register 16 (machine.h MACHINE_AY_STEREO; ignored without it), else
;	SET-UP's Stereo stands; LOOP plays the tune until stopped. Q, ESC or
;	^C stops it; the chip is silenced and the layout put back.
;	The chip is on RC2014's ports (D8h selects a register, D0h writes
;	and reads it), as for AYTEST. Works on CP/M 2.2 and 3.
;
;	Z80 mnemonics; assembled with z80pack's z80asm by build.sh, put on
;	the CPM22 and CPM32 disks with the tunes by diskimg_gen.py.

BDOS	EQU	5
FCB	EQU	5CH
FCBTYP	EQU	FCB+9
FCBEX	EQU	FCB+12
FCBCR	EQU	FCB+32
TAIL	EQU	80H
AYSEL	EQU	0D8H		;the chip's ports
AYDAT	EQU	0D0H
TICKL	EQU	68		;the millisecond tick: low (latches the high)
TICKH	EQU	69
MIXER	EQU	7
SHAPE	EQU	13
STEREO	EQU	16		;ours: the layout
FRAMEMS	EQU	20		;50 frames a second
CR	EQU	0DH
LF	EQU	0AH

	ORG	100H

START:	LD	SP,STACK
	LD	A,(FCB+1)
	CP	' '
	JP	Z,USAGE
	CALL	PROBE
	JR	Z,S1
	LD	HL,SNOAY
	JP	FAIL
S1:	LD	A,STEREO	;0FFH without it
	CALL	AYR
	CP	0FFH
	JR	Z,S2
	AND	3		;what to put back
	LD	(ST0),A
	LD	A,1
	LD	(HASST),A
S2:	CALL	TICKOK
	JR	Z,S3
	LD	HL,SNOTICK
	JP	FAIL
S3:	CALL	OPTS
	LD	A,(FCBTYP)	;no type: .PSG
	CP	' '
	JR	NZ,S4
	LD	HL,SPSG
	LD	DE,FCBTYP
	LD	BC,3
	LDIR
S4:	LD	DE,BUF
	LD	C,26		;set DMA
	CALL	BDOS
	CALL	REWIND
	INC	A
	JR	NZ,S5
	LD	HL,SNOFILE
	JP	FAIL
S5:	CALL	HEADER
	JR	Z,S6
	LD	HL,SBADPSG
	JP	FAIL
S6:	LD	A,(HASST)	;the layout asked for
	OR	A
	JR	Z,S7
	LD	A,(OPTST)
	OR	A
	JR	Z,S7
	LD	E,A
	LD	A,STEREO
	CALL	AYW
S7:	CALL	QUIET
	LD	HL,SPLAY
	CALL	PUTS
	CALL	PNAME
	LD	A,(HASST)
	OR	A
	JR	Z,S8
	LD	HL,SIN
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
S8:	LD	HL,SKEYS
	CALL	PUTS
	CALL	SHOWT
	CALL	TICK
	LD	(DUE),HL

;	The tune, a byte at a time.
PLOOP:	CALL	GETB
	JR	C,PEND
	CP	0FFH
	JR	Z,PFRAME
	CP	0FEH
	JR	Z,PWAITN
	CP	0FDH
	JR	Z,PEND
	CP	16
	JR	NC,PSKIP	;not a register: its value skipped
	LD	(REG),A
	CALL	GETB
	JR	C,PEND
	LD	E,A
	LD	A,(REG)
	CALL	AYW
	JR	PLOOP
PSKIP:	CALL	GETB
	JR	C,PEND
	JR	PLOOP
PFRAME:	CALL	FRAME
	JR	PLOOP
PWAITN:	CALL	GETB		;n x 4 frames
	JR	C,PEND
	OR	A
	JR	Z,PLOOP
	LD	B,A
PW1:	PUSH	BC
	LD	B,4
PW2:	PUSH	BC
	CALL	FRAME
	POP	BC
	DJNZ	PW2
	POP	BC
	DJNZ	PW1
	JR	PLOOP
PEND:	LD	A,(LOOPF)	;the end (or the file's)
	OR	A
	JR	Z,DONE
	CALL	REWIND
	CALL	HEADER
	JR	Z,PLOOP

;	Silence, the layout as found, back to CP/M.
DONE:	CALL	QUIET
	LD	A,(HASST)
	OR	A
	JR	Z,D1
	LD	A,(ST0)
	LD	E,A
	LD	A,STEREO
	CALL	AYW
D1:	CALL	CRLF
	JP	0

FAIL:	CALL	PUTS
	JP	0

;	---- timing ----

;	The end of a frame: wait for its time, stopping on Q, ESC or ^C;
;	more than 200 ms late (a slow disk), the clock starts again from
;	now. Counts the seconds shown.
FRAME:	LD	HL,(DUE)
	LD	DE,FRAMEMS
	ADD	HL,DE
	LD	(DUE),HL
FR1:	CALL	KEYCHK
	CALL	TICK
	LD	DE,(DUE)
	OR	A
	SBC	HL,DE		;now - due: negative while early
	BIT	7,H
	JR	NZ,FR1
	LD	A,H
	OR	A
	JR	NZ,FR2
	LD	A,L
	CP	200
	JR	C,FR3
FR2:	CALL	TICK
	LD	(DUE),HL
FR3:	LD	A,(FCNT)
	INC	A
	CP	50
	JR	C,FR4
	LD	HL,(SECS)
	INC	HL
	LD	(SECS),HL
	CALL	SHOWT
	XOR	A
FR4:	LD	(FCNT),A
	RET

;	Q, ESC or ^C: stop (other keys are dropped).
KEYCHK:	LD	C,6		;direct console input: 0 while none
	LD	E,0FFH
	CALL	BDOS
	OR	A
	RET	Z
	CP	3
	JR	Z,STOP
	CP	1BH
	JR	Z,STOP
	AND	5FH
	CP	'Q'
	RET	NZ
STOP:	LD	HL,SSTOP
	CALL	PUTS
	JP	DONE

;	HL = the millisecond tick.
TICK:	IN	A,(TICKL)
	LD	L,A
	IN	A,(TICKH)
	LD	H,A
	RET

;	Z if the tick moves within 4 x 65536 reads.
TICKOK:	CALL	TICK
	LD	(DUE),HL
	LD	B,4
TK0:	LD	DE,0
TK1:	PUSH	DE
	CALL	TICK
	LD	DE,(DUE)
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

;	---- the file ----

;	Open it from its start (extent 0, record 0); A = the BDOS's answer
;	(0FFH: not found).
REWIND:	XOR	A
	LD	(FCBEX),A
	LD	(FCBCR),A
	LD	A,128		;the buffer empty
	LD	(BPOS),A
	LD	DE,FCB
	LD	C,15
	JP	BDOS

;	A = the next byte; carry at the end of the file.
GETB:	LD	A,(BPOS)
	CP	128
	JR	C,G1
	LD	DE,FCB		;the next record
	LD	C,20
	CALL	BDOS
	OR	A
	SCF
	RET	NZ
	XOR	A
G1:	LD	E,A
	LD	D,0
	LD	HL,BUF
	ADD	HL,DE
	INC	A
	LD	(BPOS),A
	LD	A,(HL)
	OR	A		;no carry
	RET

;	Z if the header is a PSG one (its 16 bytes read).
HEADER:	LD	HL,SIG
	LD	B,4
H1:	PUSH	HL
	PUSH	BC
	CALL	GETB
	POP	BC
	POP	HL
	JR	C,H3
	CP	(HL)
	RET	NZ
	INC	HL
	DJNZ	H1
	LD	B,12
H2:	PUSH	BC
	CALL	GETB
	POP	BC
	JR	C,H3
	DJNZ	H2
	XOR	A
	RET
H3:	OR	1		;too short
	RET

;	---- the options ----

;	The words after the file name in the command tail: MONO, ABC, ACB
;	(OPTST 1-3), LOOP (LOOPF); anything else: the usage.
OPTS:	LD	HL,TAIL+1
	LD	(TOKP),HL
	LD	A,(TAIL)
	LD	(TOKN),A
	CALL	SKIPSP
	RET	Z
	CALL	SKIPTK		;the file name
OP1:	CALL	SKIPSP
	RET	Z
	LD	DE,OPTTAB
OP2:	LD	A,(DE)
	OR	A
	JR	Z,OPBAD
	CALL	MATCH
	JR	Z,OP3
	INC	DE		;its variable and value
	INC	DE
	INC	DE
	JR	OP2
OP3:	EX	DE,HL
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	LD	A,(HL)
	LD	(DE),A
	CALL	SKIPTK
	JR	OP1
OPBAD:	LD	HL,SBADOPT
	CALL	PUTS
	JP	USAGE

;	Past the spaces at TOKP; Z if nothing is left.
SKIPSP:	LD	HL,(TOKP)
	LD	A,(TOKN)
	LD	B,A
SS1:	LD	A,B
	OR	A
	JR	Z,SS3
	LD	A,(HL)
	CP	' '
	JR	NZ,SS2
	INC	HL
	DEC	B
	JR	SS1
SS2:	OR	A		;a character: NZ
SS3:	LD	(TOKP),HL
	LD	A,B
	LD	(TOKN),A
	RET

;	Past the word at TOKP.
SKIPTK:	LD	HL,(TOKP)
	LD	A,(TOKN)
	LD	B,A
ST1:	LD	A,B
	OR	A
	JR	Z,ST2
	LD	A,(HL)
	CP	' '
	JR	Z,ST2
	INC	HL
	DEC	B
	JR	ST1
ST2:	LD	(TOKP),HL
	LD	A,B
	LD	(TOKN),A
	RET

;	Z if the word at TOKP is the text at DE (to its 0); DE past the 0
;	either way.
MATCH:	LD	HL,(TOKP)
	LD	A,(TOKN)
	LD	C,A
MA1:	LD	A,(DE)
	OR	A
	JR	Z,MA2
	LD	B,A
	LD	A,C
	OR	A
	JR	Z,MA3		;the word is shorter
	LD	A,(HL)
	CP	B
	JR	NZ,MA3
	INC	HL
	INC	DE
	DEC	C
	JR	MA1
MA2:	INC	DE		;the text matched: so must the word's end
	LD	A,C
	OR	A
	RET	Z
	LD	A,(HL)
	CP	' '
	RET
MA3:	LD	A,(DE)
	INC	DE
	OR	A
	JR	NZ,MA3
	OR	1
	RET

;	---- the chip ----

;	Z if it is there: register 1 keeps 4 bits, register 2 all 8.
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

;	A = register A. Keeps BC, DE, HL.
AYR:	OUT	(AYSEL),A
	IN	A,(AYDAT)
	RET

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

;	A = the layout in use: 0 mono, 1 ABC, 2 ACB (mono without the
;	stereo register).
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
	RET	Z
	DEC	A
	RET

;	---- the console ----

;	No file named: the usage and the .PSG files here.
USAGE:	LD	HL,SUSAGE
	CALL	PUTS
	LD	DE,BUF
	LD	C,26
	CALL	BDOS
	LD	DE,LFCB
	LD	C,17		;search first
	CALL	BDOS
	CP	0FFH
	JR	NZ,U1
	LD	HL,SNONE
	CALL	PUTS
	JP	0
U1:	ADD	A,A		;the entry: BUF + 32 x A
	ADD	A,A
	ADD	A,A
	ADD	A,A
	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,BUF+1
	ADD	HL,DE
	LD	A,' '
	CALL	PUTC
	CALL	PUTC
	LD	B,8
	CALL	PFIELD
	LD	A,'.'
	CALL	PUTC
	LD	B,3
	CALL	PFIELD
	CALL	CRLF
	LD	DE,LFCB
	LD	C,18		;search next
	CALL	BDOS
	CP	0FFH
	JR	NZ,U1
	JP	0

;	B characters at HL (attribute bits off), spaces left out.
PFIELD:	LD	A,(HL)
	AND	7FH
	CP	' '
	CALL	NZ,PUTC
	INC	HL
	DJNZ	PFIELD
	RET

;	The file's name as given: d:NAME.TYP.
PNAME:	LD	A,(FCB)
	OR	A
	JR	Z,PN1
	ADD	A,'@'
	CALL	PUTC
	LD	A,':'
	CALL	PUTC
PN1:	LD	HL,FCB+1
	LD	B,8
	CALL	PFIELD
	LD	A,'.'
	CALL	PUTC
	LD	B,3
	JP	PFIELD

;	CR and the time played, m:ss.
SHOWT:	LD	A,CR
	CALL	PUTC
	LD	A,' '
	CALL	PUTC
	CALL	PUTC
	LD	HL,(SECS)
	LD	C,60
	CALL	DIVC		;HL minutes, A seconds
	PUSH	AF
	CALL	PDEC
	LD	A,':'
	CALL	PUTC
	POP	AF
	LD	L,A
	LD	H,0
	LD	C,10
	CALL	DIVC
	PUSH	AF
	LD	A,L
	ADD	A,'0'
	CALL	PUTC
	POP	AF
	ADD	A,'0'
	JP	PUTC

;	HL in decimal.
PDEC:	LD	C,10
	CALL	DIVC
	PUSH	AF
	LD	A,H
	OR	L
	CALL	NZ,PDEC
	POP	AF
	ADD	A,'0'
	JP	PUTC

;	HL = HL / C, A = the remainder (C up to 127).
DIVC:	LD	B,16
	XOR	A
DV1:	ADD	HL,HL
	RLA
	CP	C
	JR	C,DV2
	SUB	C
	INC	L
DV2:	DJNZ	DV1
	RET

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

SIG:	DB	'PSG',1AH
SPSG:	DB	'PSG'
LFCB:	DB	0,'????????PSG',0,0,0,0
OPTTAB:	DB	'MONO',0
	DW	OPTST
	DB	1
	DB	'ABC',0
	DW	OPTST
	DB	2
	DB	'ACB',0
	DW	OPTST
	DB	3
	DB	'LOOP',0
	DW	LOOPF
	DB	1
	DB	0
LNAMES:	DW	SMONO,SABC,SACB
SMONO:	DB	'mono',0
SABC:	DB	'ABC stereo',0
SACB:	DB	'ACB stereo',0

SUSAGE:	DB	'AYPLAY [d:]NAME[.PSG] [MONO|ABC|ACB] [LOOP]',CR,LF
	DB	'  plays a PSG file on the AY-3-8910 (ports D8h/D0h).',CR,LF
	DB	'  MONO, ABC or ACB: the stereo layout (else SET-UP',27H,'s);',CR,LF
	DB	'  LOOP: again until stopped. Q, ESC or ^C stops it.',CR,LF
	DB	'PSG files here:',CR,LF,0
SNONE:	DB	'  none',CR,LF,0
SPLAY:	DB	'Playing ',0
SIN:	DB	' in ',0
SKEYS:	DB	' (Q stops)',CR,LF,0
SSTOP:	DB	' -- stopped',0
SNOAY:	DB	'No AY-3-8910 on ports D8h/D0h.',CR,LF,0
SNOTICK: DB	'No millisecond tick on ports 68/69: newer DVI Feather',CR,LF
	DB	'Terminal firmware has it.',CR,LF,0
SNOFILE: DB	'No such file.',CR,LF,0
SBADPSG: DB	'Not a PSG file.',CR,LF,0
SBADOPT: DB	'Unknown option.',CR,LF,0

HASST:	DB	0		;1: the stereo register is there
ST0:	DB	0		;its layout when AYPLAY started
OPTST:	DB	0		;the layout asked for: 0 none, 1-3
LOOPF:	DB	0		;1: LOOP
FCNT:	DB	0		;frames into the second
SECS:	DW	0		;seconds played
BPOS:	DB	128		;the next byte in BUF
REG:	DB	0
DUE:	DW	0		;the next frame's end, on the tick
TOKP:	DW	0		;the command tail's words
TOKN:	DB	0
BUF:	DS	128
	DS	64
STACK:
	END	START
