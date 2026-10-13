;	XERA: erase files in the transfer folder of the SD card or a USB stick.
;
;	XERA [SD:|USB:]afn
;
;	Erases the files in /XFER on the SD card (SD:, the default) or a USB
;	stick (USB:) matching afn, naming each one. With wildcards it says
;	how many match and asks first: Y erases them, anything else leaves
;	them. Only names CP/M can take are seen, as XLS lists them; ERA
;	erases CP/M files.
;
;	The names are collected before any is erased, so erasing does not
;	disturb the listing. Uses the file transfer device (port 10,
;	src/machine/machine.h) through xfer.inc. Assembled with z80pack's z80asm by
;	build.sh, put on the CPM22 disk by diskimg_gen.py.

	ORG	100H

START:	LD	SP,STACK
	LD	HL,TAIL
	LD	B,(HL)
	INC	HL
	CALL	PARSE
	JP	C,USAGE
	CALL	SKIPSP
	JP	NZ,USAGE	;more after the spec
	LD	A,(WHASN)
	OR	A
	JP	Z,USAGE		;a name is needed: no erasing everything by default
	LD	A,(WKIND)
	CP	KDRIVE
	JP	Z,USAGE		;a CP/M drive: ERA erases those
	CALL	SETVOL

	LD	HL,WNAME	;the matching names into TABLE
	LD	DE,ENTRY
	LD	BC,11
	LDIR
	LD	HL,ENTRY
	CALL	XSETD
	LD	HL,TABLE
	LD	(TABEND),HL
	LD	A,XFIRST
	CALL	XCMD
ER1:	JR	Z,ER2
	CP	XNOTF		;the end of the listing
	JP	NZ,XFAIL
	JR	ER3
ER2:	LD	HL,ENTRY
	LD	DE,(TABEND)
	LD	BC,11
	LDIR
	LD	(TABEND),DE
	LD	HL,(COUNT)
	INC	HL
	LD	(COUNT),HL
	LD	HL,(BDOS+1)	;room for another below the BDOS
	LD	BC,-11-64
	ADD	HL,BC
	OR	A
	SBC	HL,DE
	JP	C,TOOMANY
	LD	A,XNEXT
	CALL	XCMD
	JR	ER1

ER3:	LD	HL,(COUNT)
	LD	A,H
	OR	L
	JP	Z,NOFILE
	LD	A,(WWILD)	;wildcards: ask
	OR	A
	JR	Z,ER4
	LD	HL,COUNT
	CALL	PUTCNT
	LD	HL,SASK
	CALL	PUTS
	LD	C,1		;console input
	CALL	BDOS
	CALL	UPPER
	PUSH	AF
	CALL	CRLF
	POP	AF
	CP	'Y'
	JP	NZ,0		;left as they are

ER4:	LD	HL,TABLE
	LD	(TABPTR),HL
ER5:	LD	HL,(TABPTR)
	CALL	PUTNAM
	CALL	XSETD		;the name is the DELETE's input
	LD	A,XDEL
	CALL	XCMD
	JP	NZ,XFAIL
	CALL	CRLF
	LD	HL,(TABPTR)
	LD	DE,11
	ADD	HL,DE
	LD	(TABPTR),HL
	EX	DE,HL
	LD	HL,(TABEND)
	OR	A
	SBC	HL,DE
	JR	NZ,ER5
	LD	HL,COUNT
	CALL	PUTCNT
	LD	HL,SERASED
	CALL	PUTS
	JP	0

NOFILE:	LD	HL,SNOFILE
	JR	FAIL
TOOMANY: LD	HL,STOOMNY
FAIL:	CALL	PUTS
	CALL	CRLF
	JP	STOP

USAGE:	LD	HL,SUSAGE
	CALL	PUTS
	JP	0

	INCLUDE	xfer.inc

SUSAGE:	DB	'Usage: XERA [SD:|USB:]afn',13,10
	DB	'  erases files in /XFER on the SD card (SD:, the default) or a USB stick',13,10,0
SASK:	DB	' match: erase them (Y/N)? ',0
SERASED: DB	' erased',13,10,0
SNOFILE: DB	'No file',0
STOOMNY: DB	'Too many files: erase fewer at a time',0
COUNT:	DW	0
TABEND:	DW	0
TABPTR:	DW	0
ENTRY:	DS	16		;a listing entry: the name, the size
	DS	64
STACK:
TABLE:				;names to erase, up to the BDOS

	END
