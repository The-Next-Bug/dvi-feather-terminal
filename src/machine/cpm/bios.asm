;	8080 CBIOS for the DVI terminal's Z80 machine: CP/M 2.2 with eight
;	drives, A-H, each holding whatever disk image is mounted on it: an
;	8" SSSD floppy, z80pack's 4 MB hard disk or an 8 MB hard disk. The
;	format of each drive comes from the disk controller (port DINFO) when
;	the drive is selected, so a different image needs a warm boot (^C).
;
;	Derived from z80pack's picosim CBIOS for SD-FDC machines,
;	Copyright (C) 2024 by Udo Munk (MIT license).
;
;	Assembled by src/machine/cpm/build.sh with z80pack's z80asm, which also
;	checks that the data area (ENDDAT) ends below the boot ROM at FF00H.
;
MSIZE	EQU	62		;CP/M memory size in kilobytes
;
BIAS	EQU	(MSIZE-20)*1024
CCP	EQU	3400H+BIAS	;base of ccp
BDOS	EQU	CCP+806H	;base of bdos
BIOS	EQU	CCP+1600H	;base of bios
NSECTS	EQU	(BIOS-CCP)/128	;warm start sector count
CDISK	EQU	0004H		;current disk number 0=A,...,15=P
IOBYTE	EQU	0003H		;Intel I/O byte
FDCCMD	EQU	0040H		;FDC command bytes
DDTRK	EQU	0		;offset for track
DDSEC	EQU	1		;offset for sector
DDLDMA	EQU	2		;offset for DMA address low
DDHDMA	EQU	3		;offset for DMA address high
NDRV	EQU	8		;drives A-H
;
;	I/O ports
;
CONSTA	EQU	0		;console status port
CONDAT	EQU	1		;console data port
AUXSTA	EQU	2		;SIO2 status port: the board's serial port
AUXDAT	EQU	3		;SIO2 data port
FDC	EQU	4		;port for the FDC
DINFO	EQU	8		;drive format: OUT drive, IN 0 none, 1 floppy,
				;2 4 MB hard disk, 3 8 MB hard disk
LEDS	EQU	0FFH		;frontpanel LED's
;
	ORG	BIOS		;origin of BIOS
;
;	jump vector for individual subroutines
;
	JMP	BOOT		;cold boot
WBE	JMP	WBOOT		;warm start
	JMP	CONST		;console status
	JMP	CONIN		;console character in
	JMP	CONOUT		;console character out
	JMP	LIST		;list character out
	JMP	PUNCH		;punch character out
	JMP	READER		;reader character in
	JMP	HOME		;move disk head to home position
	JMP	SELDSK		;select disk drive
	JMP	SETTRK		;set track number
	JMP	SETSEC		;set sector number
	JMP	SETDMA		;set dma address
	JMP	READ		;read disk sector
	JMP	WRITE		;write disk sector
	JMP	LISTST		;list status
	JMP	SECTRAN		;sector translate
;
;	data tables
;
SIGNON	DB	MSIZE / 10 + '0',MSIZE MOD 10 + '0'
	DB	'K CP/M 2.2, 8 drives',13,10,0
BOOTERR	DB	13,10,'BOOT ERROR',13,10,0
;
;	sector translate table for IBM 8" SD disks
TRANS	DB	1,7,13,19	;sectors 1,2,3,4
	DB	25,5,11,17	;sectors 5,6,7,8
	DB	23,3,9,15	;sectors 9,10,11,12
	DB	21,2,8,14	;sectors 13,14,15,16
	DB	20,26,6,12	;sectors 17,18,19,20
	DB	18,24,4,10	;sectors 21,22,23,24
	DB	16,22		;sectors 25,26
;
;	disk parameter blocks, by drive format (DPBTAB)
;
;	IBM 8" SD floppy: 77 tracks of 26 128-byte sectors
DPBFD	DW	26		;sectors per track
	DB	3		;block shift factor
	DB	7		;block mask
	DB	0		;extent mask
	DW	242		;disk size-1
	DW	63		;directory max
	DB	192		;alloc 0
	DB	0		;alloc 1
	DW	16		;check size
	DW	2		;track offset
;
;	z80pack 4 MB hard disk: 255 tracks of 128 sectors, 2 KB blocks
DPBHD4	DW	128		;sectors per track
	DB	4		;block shift factor
	DB	15		;block mask
	DB	0		;extent mask
	DW	2039		;disk size-1
	DW	1023		;directory max
	DB	255		;alloc 0
	DB	255		;alloc 1
	DW	0		;check size
	DW	0		;track offset
;
;	8 MB hard disk: 256 tracks of 255 sectors, 4 KB blocks
DPBHD8	DW	255		;sectors per track
	DB	5		;block shift factor
	DB	31		;block mask
	DB	1		;extent mask
	DW	2039		;disk size-1
	DW	1023		;directory max
	DB	255		;alloc 0
	DB	0		;alloc 1
	DW	0		;check size
	DW	0		;track offset
;
DPBTAB	DW	DPBFD,DPBHD4,DPBHD8
;
;	print a message to the console
;	pointer to string in hl
;
PRTMSG	MOV	A,M		;get next message byte
	ORA	A		;is it zero?
	RZ			;yes, done
	MOV	C,A		;no, print character on console
	CALL	CONOUT
	INX	H		;and do next
	JMP	PRTMSG
;
;	cold start
;
BOOT	LXI	SP,80H		;use space below buffer for stack
	LXI	H,SIGNON	;print signon
	CALL	PRTMSG
;	build the disk parameter headers: XLT and DPB are set by SELDSK,
;	the rest point at the shared directory buffer and each drive's
;	check and allocation vectors
	LXI	H,DPBASE	;HL = header
	LXI	D,ALLVEC	;DE = its allocation vector
	LXI	B,CHKVEC	;BC = its check vector
	MVI	A,NDRV
BOOT1	STA	BCOUNT
	PUSH	D
	LXI	D,8		;skip XLT and the BDOS's scratch words
	DAD	D
	POP	D
	MVI	M,DIRBF AND 0FFH
	INX	H
	MVI	M,DIRBF SHR 8
	INX	H
	INX	H		;skip DPB
	INX	H
	MOV	M,C		;CSV
	INX	H
	MOV	M,B
	INX	H
	MOV	M,E		;ALV
	INX	H
	MOV	M,D
	INX	H		;HL = next header
	PUSH	H
	LXI	H,ALVSZ		;DE = next allocation vector
	DAD	D
	XCHG
	LXI	H,CSVSZ		;BC = next check vector
	DAD	B
	MOV	B,H
	MOV	C,L
	POP	H
	LDA	BCOUNT
	DCR	A
	JNZ	BOOT1
	XRA	A		;zero in the accumulator
	STA	CDISK		;select disk drive 0
	STA	DSKNO
	STA	IOBYTE		;setup IOBYTE
	MVI	A,10H		;setup FDC command
	OUT	FDC
	MVI	A,FDCCMD AND 0FFH
	OUT	FDC
	MVI	A,FDCCMD SHR 8
	OUT	FDC
	STC			;flag for cold start
	CMC
	JMP	GOCPM		;initialize and go to CP/M
;
;	warm start
;
WBOOT	LXI	SP,80H		;use space below buffer for stack
	MVI	C,0		;select disk 0
	CALL	SELDSK
	CALL	HOME		;go to track 0
	MVI	B,NSECTS	;B counts # of sectors to load
	MVI	C,0		;C has the current track #
	MVI	D,2		;D has the next sector to load
	LXI	H,CCP		;base of CP/M
LOAD1	PUSH	B		;save sector count and current track
	PUSH	D		;save next sector to read
	PUSH	H		;save DMA address
	MOV	C,D		;get sector address to C
	CALL	SETSEC		;set sector address
	POP	B		;recall DMA address to BC
	PUSH	B		;and replace on stack for later recall
	CALL	SETDMA		;set DMA address from BC
	CALL	READ		;read sector
	ORA	A		;any errors?
	JZ	LOAD2		;no, continue
	LXI	H,BOOTERR	;otherwise print message
	CALL	PRTMSG
	HLT			;and halt the machine
LOAD2	POP	H		;recall DMA address
	LXI	D,128		;DMA = DMA + 128
	DAD	D		;next DMA address now in HL
	POP	D		;recall sector address
	POP	B		;recall # of sectors remaining
	DCR	B		;sectors = sectors - 1
	STC			;flag for warm start
	JZ	GOCPM		;transfer to CP/M if all loaded
	INR	D		;next sector
	MOV	A,D		;sector = 27 ?
	CPI	27
	JC	LOAD1		;no, continue
	MVI	D,1		;else begin with sector 1 on next track
	INR	C
	CALL	SETTRK
	JMP	LOAD1		;for another sector
GOCPM	MVI	A,0C3H		;C3 is a JMP instruction
	STA	0		;for jmp to wboot
	LXI	H,WBE		;WBOOT entry point
	SHLD	1		;set address for JMP at 0
	STA	5		;for JMP to BDOS
	LXI	H,BDOS		;BDOS entry point
	SHLD	6		;set address for JMP at 5
	LXI	B,80H		;default dma address is 80H
	CALL	SETDMA
	LDA	CDISK		;get current disk number
	MOV	C,A		;send to the CCP
	JC	CCP+3		;go to CCP warm start
	JMP	CCP		;go to CCP cold start
;
;	console status, return 0FFH if character ready, 00H if not
;
CONST	IN	CONSTA		;get console status
	RRC			;test bit 0
	JC	CONST1		;not ready
	MVI	A,0FFH		;ready, set flag
	RET
CONST1	XRA	A		;zero A
	RET
;
;	console input character into register A
;
CONIN	IN	CONSTA		;get console status
	RRC			;test bit 0
	JC	CONIN		;not ready
	IN	CONDAT		;get character from console
	RET
;
;	console output
;
CONOUT	IN	CONSTA		;get status
	RLC			;test bit 7
	JC	CONOUT		;wait until transmitter ready
	MOV	A,C		;get character into accumulator
	OUT	CONDAT		;send to console
	RET
;
;	printer status, return 0FFH if character ready, 00H if not
;
LISTST	XRA	A		;we have no printer
	RET			;so never ready
;
;	line printer output
;
LIST	RET			;we have no printer
;
;	punch character from register C: to the serial port (SIO2)
;
PUNCH	IN	AUXSTA		;get status
	RLC			;test bit 7
	JC	PUNCH		;wait until the transmit queue has room
	MOV	A,C		;get character into accumulator
	OUT	AUXDAT		;send it
	RET
;
;	read character into register A from reader: the serial port (SIO2)
;
READER	IN	AUXSTA		;get status
	RRC			;test bit 0
	JC	READER		;not ready
	IN	AUXDAT		;get character
	RET
;
;	move to track 0 position on current disk
;
HOME	MVI	C,0		;select track 0
	JMP	SETTRK
;
;	select disk given by register C: HL = its header, set up for the
;	format of the image mounted on it; 0 if there is none
;
SELDSK	LXI	H,0		;error return code
	MOV	A,C		;get disk # to accumulator
	CPI	NDRV		;disk drive < NDRV ?
	RNC			;no, return with error
	OUT	DINFO		;ask the controller for its format
	IN	DINFO
	ORA	A		;no disk?
	RZ			;return with error
	CPI	4		;known format?
	RNC			;no, return with error
	MOV	B,A		;B = format 1-3
	MOV	A,C
	STA	DSKNO		;save disk #
	MOV	L,C		;HL = disk #
	DAD	H		;*2
	DAD	H		;*4
	DAD	H		;*8
	DAD	H		;*16 (size of each header)
	LXI	D,DPBASE
	DAD	D		;HL=.DPBASE(DISKNO*16)
	PUSH	H		;the header, returned
	LXI	D,TRANS		;floppies are skewed
	MOV	A,B
	CPI	1
	JZ	SEL1
	LXI	D,0		;hard disks are not
SEL1	MOV	M,E		;XLT
	INX	H
	MOV	M,D
	LXI	D,9		;HL+1+9 = .DPB field
	DAD	D
	PUSH	H
	MOV	L,B		;HL = DPBTAB + 2 * (format - 1)
	MVI	H,0
	DAD	H
	LXI	D,DPBTAB-2
	DAD	D
	MOV	E,M		;DE = the format's DPB
	INX	H
	MOV	D,M
	POP	H
	MOV	M,E		;DPB
	INX	H
	MOV	M,D
	POP	H
	RET
;
;	set track given by register C
;
SETTRK	MOV	A,C		;get to accumulator
	STA	FDCCMD+DDTRK	;set in FDC command
	RET
;
;	set sector given by register C
;
SETSEC	MOV	A,C		;get to accumulator
	STA	FDCCMD+DDSEC	;set in FDC command
	RET
;
;	set DMA address given by registers B and C
;
SETDMA	MOV	A,C		;low order address
	STA	FDCCMD+DDLDMA	;set in FDC command
	MOV	A,B		;high order address
	STA	FDCCMD+DDHDMA	;set in FDC command
	RET
;
;	perform read operation
;
READ	LDA	DSKNO		;get disk #
	ORI	20H		;mask in read command
	JMP	DOIO
;
;	perform write operation
;
WRITE	LDA	DSKNO		;get disk #
	ORI	40H		;mask in write command
DOIO	OUT	FDC		;ask FDC to execute the command
	IN	FDC		;get status from FDC
	ORA	A		;is it zero?
	RZ			;return if OK
	CMA			;complement for LED's
	OUT	LEDS		;display the error code
	MVI	A,1		;CP/M's error code
	RET			;return with error
;
;	translate the sector given by BC using the translation table given
;	by DE; without one (hard disks), sectors count from 1
;
SECTRAN	MOV	A,D		;is there a table?
	ORA	E
	JNZ	SECT1
	MOV	H,B		;no: HL = BC + 1
	MOV	L,C
	INX	H
	RET
SECT1	XCHG			;HL=.TRANS
	DAD	B		;HL=.TRANS(SECTOR)
	MOV	L,M		;L=TRANS(SECTOR)
	MVI	H,0		;HL=TRANS(SECTOR)
	RET			;with value in HL
;
;	The remainder of the CBIOS is reserved uninitialized
;	data area, and does not need to be part of the system
;	memory image. The space must be available, however,
;	between "BEGDAT" and "ENDDAT".
;
BEGDAT	EQU	$		;begin of data area
;
ALVSZ	EQU	255		;allocation vector: the 2040 blocks of a hard disk
CSVSZ	EQU	16		;check vector: a floppy's 64 directory entries
;
DSKNO	DS	1		;selected disk
BCOUNT	DS	1		;cold boot's loop count
DPBASE	DS	16*NDRV		;disk parameter headers, built by BOOT
DIRBF	DS	128		;scratch directory area
ALLVEC	DS	ALVSZ*NDRV	;allocation vectors
CHKVEC	DS	CSVSZ*NDRV	;check vectors
;
ENDDAT	EQU	$		;end of data area
DATSIZ	EQU	$-BEGDAT	;size of data area
;
	END			;of CBIOS
