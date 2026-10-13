;	XPIP: copy files between CP/M drives and the transfer folder of the
;	SD card or a USB stick, in the manner of PIP.
;
;	XPIP dest=source [T]
;
;	One side is a CP/M drive (d: or the current drive), the other SD:
;	(/XFER on the SD card) or USB: (/XFER on a USB stick):
;
;	XPIP SD:=B:*.TXT		CP/M to the card, names kept
;	XPIP A:=SD:*.COM		the card to CP/M, names kept
;	XPIP SD:NEW.TXT=OLD.TXT		one file, renamed
;	XPIP SD:LOG.TXT=B:LOG.TXT[T]	a text file
;
;	A source with wildcards needs a destination without a name. As with
;	PIP, a file of the same name at the destination is replaced. CP/M
;	files are whole 128-byte records: copied to CP/M, a file's last
;	record is padded with ^Z; copied to SD: or USB:, every record is
;	kept, and [T] cuts the file at the first ^Z of its last record
;	(text). Any key stops it between files.
;
;	Uses the file transfer device (port 10, src/machine/machine.h) through
;	xfer.inc. Assembled with z80pack's z80asm by build.sh, put on the
;	CPM22 disk by diskimg_gen.py.

SPECSZ	EQU	15		;a spec: kind, drive, name (11), has a name, wild
SKIND	EQU	0		;offsets in a spec (WKIND's layout)
SDRIVE	EQU	1
SNAME	EQU	2
SHASN	EQU	13
SWILD	EQU	14

	ORG	100H

START:	LD	SP,STACK
	LD	HL,TAIL
	LD	B,(HL)
	INC	HL
	CALL	PARSE		;dest
	JP	C,USAGE
	LD	DE,DST
	CALL	SAVSPEC
	CALL	SKIPSP
	JP	Z,USAGE
	LD	A,(HL)
	CP	'='
	JP	NZ,USAGE
	INC	HL
	DEC	B
	CALL	PARSE		;source
	JP	C,USAGE
	LD	DE,SRC
	CALL	SAVSPEC
	CALL	SKIPSP		;[T]
	JR	Z,OPTS3
	LD	A,(HL)
	CP	'['
	JP	NZ,USAGE
OPTS1:	INC	HL
	DEC	B
	CALL	SKIPSP
	JR	Z,OPTS3		;no ]: as PIP, fine
	LD	A,(HL)
	CP	']'
	JR	Z,OPTS2
	CALL	UPPER
	CP	'T'
	JP	NZ,USAGE
	LD	A,1
	LD	(TEXT),A
	JR	OPTS1
OPTS2:	INC	HL
	DEC	B
	CALL	SKIPSP
	JP	NZ,USAGE

OPTS3:	LD	A,(SRC+SHASN)	;a source file
	OR	A
	JP	Z,USAGE
	LD	A,(DST+SHASN)	;one file to a name
	OR	A
	JR	Z,DIR1
	LD	A,(DST+SWILD)
	OR	A
	JP	NZ,USAGE
	LD	A,(SRC+SWILD)
	OR	A
	JP	NZ,USAGE
DIR1:	LD	A,(SRC+SKIND)	;which way
	CP	KSD
	JR	NC,DIR2
	LD	A,(DST+SKIND)	;CP/M to the card or stick
	CP	KSD
	JP	C,USAGE
	CALL	SETVOL
	JP	EXPORT
DIR2:	CALL	SETVOL		;the card or stick to CP/M
	LD	A,(DST+SKIND)
	CP	KSD
	JP	NC,USAGE
	JP	IMPORT

;	The parsed spec (WKIND...) to DE; keeps HL and B.
SAVSPEC: PUSH	HL
	PUSH	BC
	LD	HL,WKIND
	LD	BC,SPECSZ
	LDIR
	POP	BC
	POP	HL
	RET

;	---- the card or stick to CP/M ----

IMPORT:	LD	HL,SRC+SNAME
	LD	DE,ENTRY
	LD	BC,11
	LDIR
	LD	HL,ENTRY
	CALL	XSETD
	LD	A,XFIRST
	CALL	XCMD
	JR	Z,IM1
	CP	XNOTF
	JP	Z,NOFILE
	JP	XFAIL
IM1:	CALL	KEYSTOP
	CALL	IMFILE
	LD	HL,ENTRY	;the listing keeps its place meanwhile
	CALL	XSETD
	LD	A,XNEXT
	CALL	XCMD
	JR	Z,IM1
	CP	XNOTF
	JP	NZ,XFAIL
	JP	DONE

;	Copy the file of the listing entry at ENTRY to the destination.
IMFILE:	LD	HL,ENTRY
	CALL	PUTNAM
	LD	A,(DST+SDRIVE)	;the CP/M file: replaced
	LD	HL,ENTRY
	CALL	DSTNAME
	LD	DE,FCB
	LD	C,19		;delete
	CALL	BDOS
	CALL	CLRFCB
	LD	DE,FCB
	LD	C,22		;make
	CALL	BDOS
	INC	A
	JP	Z,DIRFULL
	LD	HL,ENTRY	;the source
	CALL	XSETD
	LD	A,XOPEN
	CALL	XCMD
	JP	NZ,XFAIL
	LD	HL,DMABUF
	CALL	XSETD
	LD	DE,DMABUF
	LD	C,26		;set DMA
	CALL	BDOS
IM2:	LD	A,XREAD
	CALL	XCMD
	JR	Z,IM3
	CP	XEOF
	JR	Z,IM4
	JP	XFAIL
IM3:	LD	DE,FCB
	LD	C,21		;write sequential
	CALL	BDOS
	OR	A
	JP	NZ,DSKFULL
	JR	IM2
IM4:	LD	DE,FCB
	LD	C,16		;close
	CALL	BDOS
	INC	A
	JP	Z,DSKFULL
	LD	A,XCLOSE
	CALL	XCMD
	JP	NZ,XFAIL
	JP	COPIED

;	---- CP/M to the card or stick ----

;	The matching files first (opening files would lose the search's
;	place), into TABLE, 11 bytes each.
EXPORT:	LD	A,(SRC+SDRIVE)
	LD	HL,SRC+SNAME
	CALL	MKFCB
	LD	DE,DMABUF
	LD	C,26		;set DMA
	CALL	BDOS
	LD	HL,TABLE
	LD	(TABEND),HL
	LD	DE,FCB
	LD	C,17		;search first
	CALL	BDOS
EX1:	CP	0FFH
	JR	Z,EX3
	ADD	A,A		;the entry: DMABUF + 32 * A, its name from +1
	ADD	A,A
	ADD	A,A
	ADD	A,A
	ADD	A,A
	ADD	A,DMABUF+1
	LD	L,A
	LD	H,0
	LD	DE,(TABEND)
	LD	B,11
EX2:	LD	A,(HL)
	AND	7FH		;no attribute bits
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	EX2
	LD	(TABEND),DE
	LD	HL,(BDOS+1)	;room for another below the BDOS
	LD	BC,-11-64
	ADD	HL,BC
	OR	A
	SBC	HL,DE
	JP	C,TOOMANY
	LD	DE,FCB
	LD	C,18		;search next
	CALL	BDOS
	JR	EX1
EX3:	LD	HL,(TABEND)
	LD	DE,TABLE
	OR	A
	SBC	HL,DE
	JP	Z,NOFILE
	LD	(TABPTR),DE
EX4:	CALL	KEYSTOP
	CALL	EXFILE
	LD	HL,(TABPTR)
	LD	DE,11
	ADD	HL,DE
	LD	(TABPTR),HL
	EX	DE,HL
	LD	HL,(TABEND)
	OR	A
	SBC	HL,DE
	JR	NZ,EX4
	JP	DONE

;	Copy the CP/M file named at (TABPTR) to the card or stick.
EXFILE:	LD	HL,(TABPTR)
	CALL	PUTNAM
	XOR	A		;the device's name for it: NAMEB
	LD	HL,(TABPTR)
	CALL	DSTNAME
	LD	HL,FCB+1
	LD	DE,NAMEB
	LD	BC,11
	LDIR
	LD	HL,NAMEB	;replaced
	CALL	XSETD
	LD	A,XDEL
	CALL	XCMD
	JR	Z,EF1
	CP	XNOTF
	JP	NZ,XFAIL
EF1:	LD	A,XCREATE
	CALL	XCMD
	JP	NZ,XFAIL
	LD	A,(SRC+SDRIVE)	;the source
	LD	HL,(TABPTR)
	CALL	MKFCB
	LD	DE,FCB
	LD	C,15		;open
	CALL	BDOS
	INC	A
	JP	Z,NOOPEN
	LD	HL,DMABUF
	CALL	XSETD
	LD	DE,DMABUF
	LD	C,26		;set DMA
	CALL	BDOS
EF2:	LD	DE,FCB
	LD	C,20		;read sequential
	CALL	BDOS
	OR	A
	JR	NZ,EF3		;the end
	LD	A,XWRITE
	CALL	XCMD
	JP	NZ,XFAIL
	JR	EF2
EF3:	LD	A,(TEXT)
	OR	A
	LD	A,XCLOSE
	JR	Z,EF4
	LD	A,XCLOSET
EF4:	CALL	XCMD
	JP	NZ,XFAIL
	JP	COPIED

;	---- common ----

;	The destination's name into FCB+1 (and its drive A into FCB): the
;	destination spec's name if it has one, else the source's at HL.
DSTNAME: PUSH	AF
	LD	A,(DST+SHASN)
	OR	A
	JR	Z,DN1
	LD	HL,DST+SNAME
DN1:	POP	AF
	JP	MKFCB

;	FCB: drive A, the name at HL, the rest 0.
MKFCB:	LD	(FCB),A
	LD	DE,FCB+1
	LD	BC,11
	LDIR
CLRFCB:	LD	HL,FCB+12
	LD	B,36-12
CF1:	LD	(HL),0
	INC	HL
	DJNZ	CF1
	RET

;	One more copied: end the line, count it.
COPIED:	CALL	CRLF
	LD	HL,(COUNT)
	INC	HL
	LD	(COUNT),HL
	RET

;	Any key stops it (between files).
KEYSTOP: LD	C,11		;console status
	CALL	BDOS
	OR	A
	RET	Z
	LD	C,1		;the key
	CALL	BDOS
	LD	HL,SSTOP
	JR	FAIL

DONE:	LD	HL,COUNT
	CALL	PUTCNT
	LD	HL,SCOPIED
	CALL	PUTS
	JP	0

NOFILE:	LD	HL,SNOFILE
	JR	FAIL
DIRFULL: LD	HL,SDIRFUL
	JR	FAIL
DSKFULL: LD	HL,SDSKFUL
	JR	FAIL
NOOPEN:	LD	HL,SNOOPEN
	JR	FAIL
TOOMANY: LD	HL,STOOMNY
FAIL:	CALL	PUTS
	CALL	CRLF
	JP	STOP

USAGE:	LD	HL,SUSAGE
	CALL	PUTS
	JP	0

	INCLUDE	xfer.inc

SUSAGE:	DB	'Usage: XPIP dest=source [T]',13,10
	DB	'  one side a CP/M drive, the other SD: or USB: (their /XFER folder):',13,10
	DB	'  XPIP SD:=B:*.TXT  XPIP A:=SD:*.COM  XPIP SD:NEW.TXT=OLD.TXT',13,10
	DB	'  [T] text: cut at the first ^Z when copying to SD: or USB:',13,10,0
SNOFILE: DB	'No file',0
SDIRFUL: DB	' - CP/M directory full',0
SDSKFUL: DB	' - CP/M disk full',0
SNOOPEN: DB	' - cannot open it',0
STOOMNY: DB	'Too many files: copy fewer at a time',0
SSTOP:	DB	'Stopped',0
SCOPIED: DB	' copied',13,10,0
TEXT:	DB	0
COUNT:	DW	0
TABEND:	DW	0
TABPTR:	DW	0
DST:	DS	SPECSZ
SRC:	DS	SPECSZ
ENTRY:	DS	16		;a listing entry: the name, the size
NAMEB:	DS	11
FCB:	DS	36
	DS	64
STACK:
TABLE:				;names to copy (EXPORT), up to the BDOS

	END
