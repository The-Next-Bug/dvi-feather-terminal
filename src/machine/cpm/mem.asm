;	MEM: the machine's memory, and how CP/M uses it.
;
;	MEM
;
;	The CP/M version; the TPA, the room programs get (0100H up to the
;	BDOS entry at 0006H, which an RSX such as CP/M 3's VT100DYN moves
;	down); and the memory, from the MMU (port 64, src/machine/machine.h: the
;	number of 48 KB banks besides bank 0 in the high nibble, the
;	selected bank in the low). Bank 0 is 64 KB; selecting another
;	replaces 0000H-BFFFH, and C000H-FFFFH is common to all. The bank
;	MEM runs in is the TPA's: 0 under CP/M 2.2, 1 under banked CP/M 3,
;	whose bank 0 holds its banked BDOS and BIOS and the disk buffers.
;
;	Z80 mnemonics; assembled with z80pack's z80asm by build.sh, put on
;	the CPM22 and CPM32 disks by diskimg_gen.py.

BDOS	EQU	5
MMU	EQU	64
CR	EQU	0DH
LF	EQU	0AH

	ORG	100H

START:	LD	SP,STACK
	LD	C,12		;version: L the number, H bit 0 MP/M
	CALL	BDOS
	LD	A,L
	LD	(VERS),A
	LD	A,H
	AND	1
	LD	HL,SCPM
	JR	Z,V1
	LD	HL,SMPM
V1:	CALL	PUTS
	LD	A,(VERS)	;3.1 from 31H
	RRCA
	RRCA
	RRCA
	RRCA
	CALL	PUTNIB
	LD	A,'.'
	CALL	PUTC
	LD	A,(VERS)
	CALL	PUTNIB
	CALL	CRLF

	LD	HL,STPA		;TPA     0100H-xxxxH: n bytes (nK)
	CALL	PUTS
	LD	HL,(BDOS+1)
	DEC	HL
	CALL	PUTHEX
	LD	HL,SCOLON
	CALL	PUTS
	LD	HL,(BDOS+1)
	LD	DE,-100H
	ADD	HL,DE
	PUSH	HL
	CALL	PUTDEC
	LD	HL,SBYTES
	CALL	PUTS
	POP	HL
	LD	A,H		;HL / 1024
	RRCA
	RRCA
	AND	3FH
	CALL	PUTDA
	LD	HL,SKEND
	CALL	PUTS

	IN	A,(MMU)
	LD	(MMUV),A
	RRCA
	RRCA
	RRCA
	RRCA
	AND	0FH
	LD	(NBANK),A
	LD	HL,SMEM		;Memory  nK: bank 0 64K, ...
	CALL	PUTS
	LD	A,(NBANK)	;64 + 48 * banks
	LD	L,A
	LD	H,0
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	LD	D,H
	LD	E,L
	ADD	HL,HL
	ADD	HL,DE
	LD	DE,64
	ADD	HL,DE
	CALL	PUTDEC
	LD	HL,SMEM0
	CALL	PUTS
	LD	A,(NBANK)
	OR	A
	JR	Z,M2
	LD	HL,SMEM1	;, bank 1 48K or , banks 1-n 48K each
	CP	1
	JR	Z,M1
	LD	HL,SMEMN
	CALL	PUTS
	LD	A,(NBANK)
	CALL	PUTDA
	LD	HL,SEACH
M1:	CALL	PUTS
	LD	HL,SCOMM
	CALL	PUTS
M2:	CALL	CRLF

	LD	B,0		;a line for each bank
BANKS:	LD	HL,SBANK
	CALL	PUTS
	LD	A,B
	CALL	PUTDA
	LD	HL,SGAP
	CALL	PUTS
	LD	A,(MMUV)	;the TPA's?
	AND	0FH
	CP	B
	LD	HL,STPAB
	JR	Z,B2
	LD	A,B		;bank 0 under a banked system
	OR	A
	LD	HL,SSYS
	JR	Z,B1
	LD	HL,SFREE
B1:	LD	A,(VERS)	;CP/M 3 says what bank 0 holds
	CP	30H
	JR	C,B2
	LD	A,B
	OR	A
	JR	NZ,B2
	LD	HL,SSYS3
B2:	CALL	PUTS
	CALL	CRLF
	LD	A,(NBANK)
	CP	B
	JR	Z,DONE
	INC	B
	JR	BANKS
DONE:	JP	0

;	A (0-255) in decimal; keeps BC.
PUTDA:	LD	L,A
	LD	H,0
;	HL in decimal, no leading zeros; keeps BC.
PUTDEC:	PUSH	BC
	LD	C,0		;a digit printed
	LD	DE,10000
	CALL	PD1
	LD	DE,1000
	CALL	PD1
	LD	DE,100
	CALL	PD1
	LD	DE,10
	CALL	PD1
	LD	A,L
	ADD	A,'0'
	CALL	PUTC
	POP	BC
	RET
PD1:	LD	B,'0'
PD2:	OR	A
	SBC	HL,DE
	JR	C,PD3
	INC	B
	JR	PD2
PD3:	ADD	HL,DE
	LD	A,B
	CP	'0'
	JR	NZ,PD4
	INC	C
	DEC	C
	RET	Z		;a leading zero
PD4:	LD	C,1
	JP	PUTC

;	HL as four hex digits and an H.
PUTHEX:	LD	A,H
	CALL	PUTHB
	LD	A,L
	CALL	PUTHB
	LD	A,'H'
	JP	PUTC
PUTHB:	PUSH	AF
	RRCA
	RRCA
	RRCA
	RRCA
	CALL	PUTNIB
	POP	AF
;	The low nibble of A as a hex digit.
PUTNIB:	AND	0FH
	ADD	A,'0'
	CP	'9'+1
	JR	C,PUTC
	ADD	A,'A'-'9'-1
	JR	PUTC

CRLF:	LD	A,CR
	CALL	PUTC
	LD	A,LF
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

;	The 0-ended text at HL; keeps BC.
PUTS:	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PUTC
	INC	HL
	JR	PUTS

SCPM:	DB	'CP/M ',0
SMPM:	DB	'MP/M ',0
STPA:	DB	'TPA     0100H-',0
SCOLON:	DB	': ',0
SBYTES:	DB	' bytes (',0
SKEND:	DB	'K)',CR,LF,0
SMEM:	DB	'Memory  ',0
SMEM0:	DB	'K: bank 0 64K',0
SMEM1:	DB	', bank 1 48K',0
SMEMN:	DB	', banks 1-',0
SEACH:	DB	' 48K each',0
SCOMM:	DB	' (C000H-FFFFH common)',0
SBANK:	DB	'Bank ',0
SGAP:	DB	'  ',0
STPAB:	DB	'the TPA (programs)',0
SSYS:	DB	'the system',0
SSYS3:	DB	'the system: banked BDOS and BIOS, disk buffers',0
SFREE:	DB	'not used by CP/M',0

VERS:	DB	0
MMUV:	DB	0
NBANK:	DB	0
	DS	64
STACK:

	END
