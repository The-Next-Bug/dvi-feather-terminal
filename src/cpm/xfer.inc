;	XFER.INC: shared by XLS and XPIP (included at their end): the file
;	transfer device (port 10, src/machine.h) and a command-tail parser
;	for file specs that may name SD: or USB: (the /XFER folder of the SD
;	card or a USB stick), which the CCP's FCBs cannot hold.

BDOS	EQU	5
TAIL	EQU	80H		;command tail: length, then the text
DMABUF	EQU	80H		;the default DMA buffer: records go through it

XPORT	EQU	10		;the transfer device
XRESET	EQU	00H		;commands (high nibble; low nibble: the volume)
XSETDMA	EQU	10H
XFIRST	EQU	20H
XNEXT	EQU	30H
XOPEN	EQU	40H
XCREATE	EQU	50H
XREAD	EQU	60H
XWRITE	EQU	70H
XCLOSE	EQU	80H
XCLOSET	EQU	90H		;close, cut at the first ^Z of the last record
XDEL	EQU	0A0H
XOK	EQU	0		;statuses
XNOTF	EQU	1
XEXIST	EQU	2
XEOF	EQU	3
XIOERR	EQU	4
XNOVOL	EQU	5
XBAD	EQU	6

KNONE	EQU	0		;a spec's kind: no device (the current drive)
KDRIVE	EQU	1		;a CP/M drive, A: to P:
KSD	EQU	2		;SD:
KUSB	EQU	3		;USB:
TOKMAX	EQU	16		;"USB:ABCDEFGH.EXT"

;	---- the transfer device ----

;	Point the device's DMA at HL.
XSETD:	LD	A,XSETDMA
	OUT	(XPORT),A
	LD	A,L
	OUT	(XPORT),A
	LD	A,H
	OUT	(XPORT),A
	RET

;	Command A on volume VOL; the status in A, Z if it is XOK.
XCMD:	PUSH	HL
	LD	HL,VOL
	OR	(HL)
	POP	HL
	OUT	(XPORT),A
	IN	A,(XPORT)
	OR	A
	RET

;	VOL from a spec's kind in A: 1 for USB:, else 0 (SD:).
SETVOL:	CP	KUSB
	LD	A,0
	JR	NZ,SETV1
	INC	A
SETV1:	LD	(VOL),A
	RET

;	Status A as a message, then close what is open and stop.
XFAIL:	PUSH	AF
	LD	HL,SDASH
	CALL	PUTS
	POP	AF
	LD	HL,SXIO
	CP	XIOERR
	JR	Z,XF1
	LD	HL,SXBAD
	CP	XBAD
	JR	Z,XF1
	LD	HL,SXEX
	CP	XEXIST
	JR	Z,XF1
	LD	HL,SXNF
	CP	XNOTF
	JR	Z,XF1
	LD	HL,SXUSB
	LD	A,(VOL)
	OR	A
	JR	NZ,XF1
	LD	HL,SXSD
XF1:	CALL	PUTS
	CALL	CRLF
STOP:	LD	A,XRESET
	CALL	XCMD
	JP	0

;	---- the command tail ----

;	Parse a file spec at HL (B characters left) into WKIND, WDRIVE,
;	WNAME (FCB form: blank-padded, * filled with ?), WHASN (it has a
;	name) and WWILD (the name has ?). A spec is [d:|SD:|USB:][name.typ];
;	it ends at a blank, =, [ or the end. HL and B are left after it.
;	Carry if it is bad.
PARSE:	XOR	A
	LD	(WKIND),A
	LD	(WDRIVE),A
	LD	(WHASN),A
	LD	(WWILD),A
	PUSH	HL
	LD	HL,WNAME
	LD	C,11
PA0:	LD	(HL),' '
	INC	HL
	DEC	C
	JR	NZ,PA0
	POP	HL
	CALL	SKIPSP
	LD	DE,TOK		;the spec's text, upper case, into TOK
	LD	C,0
PA1:	LD	A,B
	OR	A
	JR	Z,PA2
	LD	A,(HL)
	CALL	ISDELIM
	JR	Z,PA2
	CALL	UPPER
	LD	(DE),A
	INC	DE
	INC	HL
	DEC	B
	INC	C
	LD	A,C
	CP	TOKMAX+1
	JR	C,PA1
	SCF			;too long
	RET
PA2:	XOR	A
	LD	(DE),A
	PUSH	HL
	PUSH	BC
	LD	A,(TOK+1)	;d:
	CP	':'
	JR	NZ,PA3
	LD	A,(TOK)
	SUB	'A'
	JP	C,PBAD
	CP	16
	JP	NC,PBAD
	INC	A
	LD	(WDRIVE),A
	LD	A,KDRIVE
	LD	(WKIND),A
	LD	HL,TOK+2
	JR	PNAME
PA3:	LD	HL,TOK		;SD: or USB:
	LD	DE,SSDC
	CALL	PREFIX
	LD	A,KSD
	JR	Z,PA4
	LD	HL,TOK
	LD	DE,SUSBC
	CALL	PREFIX
	LD	A,KUSB
	JR	Z,PA4
	LD	HL,TOK		;no device: a : anywhere is bad
PA5:	LD	A,(HL)
	OR	A
	JR	Z,PA6
	CP	':'
	JP	Z,PBAD
	INC	HL
	JR	PA5
PA6:	LD	HL,TOK
	JR	PNAME
PA4:	LD	(WKIND),A
PNAME:	LD	A,(HL)		;HL: the name's text
	OR	A
	JR	Z,PDONE		;none
	CP	'.'
	JP	Z,PBAD
	LD	A,1
	LD	(WHASN),A
	LD	DE,WNAME
	LD	C,8
	CALL	PPART
	JP	C,PBAD
	LD	A,(HL)
	OR	A
	JR	Z,PWILD
	INC	HL		;past the .
	LD	DE,WNAME+8
	LD	C,3
	CALL	PPART
	JP	C,PBAD
	LD	A,(HL)
	OR	A		;a second .
	JP	NZ,PBAD
PWILD:	LD	HL,WNAME
	LD	C,11
PW1:	LD	A,(HL)
	CP	'?'
	JR	NZ,PW2
	LD	A,1
	LD	(WWILD),A
PW2:	INC	HL
	DEC	C
	JR	NZ,PW1
PDONE:	POP	BC
	POP	HL
	OR	A		;carry clear
	RET
PBAD:	POP	BC
	POP	HL
	SCF
	RET

;	Copy a name part from HL to DE: at most C characters, up to a . or
;	the end; * fills the rest with ?. Carry if it is too long.
PPART:	LD	A,(HL)
	OR	A
	RET	Z
	CP	'.'
	RET	Z
	CP	'*'
	JR	Z,PPSTAR
	INC	C
	DEC	C
	SCF
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	DEC	C
	JR	PPART
PPSTAR:	INC	HL
PPS1:	INC	C
	DEC	C
	JR	Z,PPS2
	LD	A,'?'
	LD	(DE),A
	INC	DE
	DEC	C
	JR	PPS1
PPS2:	LD	A,(HL)		;only a . or the end after the *
	OR	A
	RET	Z
	CP	'.'
	RET	Z
	SCF
	RET

;	Z if the 0-ended text at DE starts the text at HL.
PREFIX:	LD	A,(DE)
	OR	A
	RET	Z
	CP	(HL)
	RET	NZ
	INC	HL
	INC	DE
	JR	PREFIX

;	Z if A ends a spec.
ISDELIM: CP	' '
	RET	Z
	CP	9
	RET	Z
	CP	'='
	RET	Z
	CP	'['
	RET

UPPER:	CP	'a'
	RET	C
	CP	'z'+1
	RET	NC
	SUB	20H
	RET

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

;	---- output ----

;	A to the console; keeps BC, DE, HL.
PUTC:	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	C,2
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	RET

;	The 0-ended text at HL.
PUTS:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	HL
	JR	PUTS

CRLF:	LD	A,13
	CALL	PUTC
	LD	A,10
	JP	PUTC

;	The FCB-form name at HL as NAME.TYP; C: the characters shown. Keeps
;	HL.
PUTNAM:	PUSH	HL
	LD	C,0
	LD	B,8
PN1:	LD	A,(HL)
	AND	7FH
	CP	' '
	CALL	NZ,PUTNC
	INC	HL
	DJNZ	PN1
	LD	A,(HL)
	AND	7FH
	CP	' '
	JR	Z,PN3
	LD	A,'.'
	CALL	PUTNC
	LD	B,3
PN2:	LD	A,(HL)
	AND	7FH
	CP	' '
	CALL	NZ,PUTNC
	INC	HL
	DJNZ	PN2
PN3:	POP	HL
	RET
PUTNC:	INC	C
	JP	PUTC

;	The 32-bit number at HL (little-endian) in decimal, in 10 columns
;	if PAD is set, else as long as it is.
PUTD32:	LD	DE,NUM
	LD	BC,4
	LDIR
	LD	HL,POW10
	LD	C,0		;C: a digit shown (no more blanks)
	LD	B,10		;B: digits to go
PD1:	LD	D,'0'
PD2:	CALL	SUB32
	JR	C,PD3
	INC	D
	JR	PD2
PD3:	LD	A,D
	CP	'0'
	JR	NZ,PD5
	LD	A,C
	OR	A
	JR	NZ,PD5		;a 0 inside the number
	LD	A,B
	DEC	A
	JR	Z,PD5		;the last digit
	LD	A,(PAD)
	OR	A
	JR	Z,PD6
	LD	A,' '
	CALL	PUTC
	JR	PD6
PD5:	LD	A,D
	CALL	PUTC
	LD	C,1
PD6:	INC	HL
	INC	HL
	INC	HL
	INC	HL
	DJNZ	PD1
	RET

;	NUM -= the 32-bit number at HL, unless it would go below 0 (then
;	carry, NUM as it was). Keeps BC, DE, HL.
SUB32:	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	DE,NUM
	LD	IX,TMP32
	LD	B,4
	OR	A
S32A:	LD	A,(DE)
	SBC	A,(HL)
	LD	(IX+0),A
	INC	DE
	INC	HL
	INC	IX
	DJNZ	S32A
	JR	C,S32B
	LD	HL,TMP32
	LD	DE,NUM
	LD	BC,4
	LDIR			;keeps carry clear
S32B:	POP	HL
	POP	DE
	POP	BC
	RET

;	The 16-bit count at (HL) without padding.
PUTCNT:	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	LD	HL,CNT32
	LD	(HL),E
	INC	HL
	LD	(HL),D
	INC	HL
	XOR	A
	LD	(HL),A
	INC	HL
	LD	(HL),A
	LD	(PAD),A
	LD	HL,CNT32
	JP	PUTD32

POW10:	DW	0CA00H,3B9AH,0E100H,05F5H,9680H,0098H,4240H,000FH
	DW	86A0H,0001H,2710H,0,03E8H,0,0064H,0,000AH,0,0001H,0

SSDC:	DB	'SD:',0
SUSBC:	DB	'USB:',0
SDASH:	DB	' - ',0
SXIO:	DB	'card error, or the card is full',0
SXBAD:	DB	'bad name',0
SXEX:	DB	'exists',0
SXNF:	DB	'not found',0
SXSD:	DB	'no SD card (or no FAT file system on it)',0
SXUSB:	DB	'no USB stick (or no FAT file system on it)',0

VOL:	DB	0
PAD:	DB	1
WKIND:	DB	0
WDRIVE:	DB	0
WNAME:	DS	11
WHASN:	DB	0
WWILD:	DB	0
TOK:	DS	TOKMAX+1
NUM:	DS	4
TMP32:	DS	4
CNT32:	DS	4
