;	XLS: list the transfer folder of the SD card or a USB stick.
;
;	XLS [SD:|USB:][afn]
;
;	The files in /XFER on the SD card (SD:, the default) or a USB stick
;	(USB:) matching afn (default *.*), with their sizes in bytes; XPIP
;	copies them. Only names CP/M can take are listed (8.3, no _ and the
;	like); a PC's long names show as their short forms (LONGFI~1.TXT).
;	It stops after every PAGE lines until a key: Q, ESC or ^C end it.
;
;	Uses the file transfer device (port 10, src/machine/machine.h) through
;	xfer.inc. Assembled with z80pack's z80asm by build.sh, put on the
;	CPM22 disk by diskimg_gen.py.

PAGE	EQU	22		;lines a screen: 24 rows, with the prompt below

	ORG	100H

START:	LD	SP,STACK
	LD	HL,TAIL
	LD	B,(HL)
	INC	HL
	CALL	PARSE
	JP	C,USAGE
	CALL	SKIPSP
	JP	NZ,USAGE	;more after the spec
	LD	A,(WKIND)
	CP	KDRIVE
	JP	Z,USAGE		;a CP/M drive: DIR lists those
	CALL	SETVOL
	LD	A,(WHASN)
	OR	A
	JR	NZ,LS1
	LD	HL,WNAME	;no name: *.*
	LD	B,11
LS0:	LD	(HL),'?'
	INC	HL
	DJNZ	LS0
LS1:	LD	HL,WNAME
	LD	DE,ENTRY
	LD	BC,11
	LDIR
	LD	HL,ENTRY
	CALL	XSETD
	LD	A,XFIRST
	CALL	XCMD
	JR	Z,LS2
	CP	XNOTF
	JP	NZ,XFAIL
	LD	HL,SNONE
	CALL	PUTS
	JP	0
LS2:	LD	HL,ENTRY	;NAME.TYP, then the size in 10 columns
	CALL	PUTNAM
LS3:	LD	A,C
	CP	13
	JR	NC,LS4
	LD	A,' '
	CALL	PUTC
	INC	C
	JR	LS3
LS4:	LD	HL,ENTRY+11
	CALL	PUTD32
	CALL	CRLF
	LD	HL,(COUNT)
	INC	HL
	LD	(COUNT),HL
	CALL	PAGING
	LD	A,XNEXT
	CALL	XCMD
	JR	Z,LS2
	CP	XNOTF
	JP	NZ,XFAIL
	LD	HL,COUNT
	CALL	PUTCNT
	LD	HL,SFILES
	CALL	PUTS
	LD	HL,(COUNT)
	DEC	HL
	LD	A,H
	OR	L
	LD	A,'s'
	CALL	NZ,PUTC
	CALL	CRLF
	JP	0

; A screen full: wait for a key before the next line; Q, ESC or ^C end it.
PAGING:	LD	HL,LINES
	INC	(HL)
	LD	A,(HL)
	CP	PAGE
	RET	C
	LD	(HL),0
	LD	HL,SMORE
	CALL	PUTS
PG1:	LD	E,0FFH		;direct console input: 0 while no key
	LD	C,6
	CALL	BDOS
	OR	A
	JR	Z,PG1
	CALL	UPPER
	PUSH	AF
	LD	HL,SUNMORE	;the prompt off the screen
	CALL	PUTS
	POP	AF
	CP	'Q'
	JR	Z,PG2
	CP	1BH
	JR	Z,PG2
	CP	3
	RET	NZ
PG2:	JP	STOP		;closes the listing

USAGE:	LD	HL,SUSAGE
	CALL	PUTS
	JP	0

	INCLUDE	xfer.inc

SUSAGE:	DB	'Usage: XLS [SD:|USB:][afn]',13,10
	DB	'  lists /XFER on the SD card (SD:, the default) or a USB stick',13,10,0
SNONE:	DB	'No files',13,10,0
SFILES:	DB	' file',0
SMORE:	DB	'-- more (Q stops) --',0
SUNMORE: DB	13,'                    ',13,0
LINES:	DB	0
COUNT:	DW	0
ENTRY:	DS	16		;a listing entry: the name, the size
	DS	64
STACK:

	END
