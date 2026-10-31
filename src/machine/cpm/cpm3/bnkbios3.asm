;
;	CP/M 3 banked BIOS for z80pack machines using SD-FDC
;
;	Copyright (C) 2024 by Udo Munk (z80pack, MIT license)
;
;	The dvi-feather-terminal's version (bd f8w.4): eight drives A-H,
;	each of the formats the disk controller's port 8 reports for the
;	image mounted on it (8" floppy, 4 MB or 8 MB hard disk), as our
;	CP/M 2.2 BIOS (src/machine/cpm/bios.asm) does. Assembled with RMAC, linked
;	and made into CPM3.SYS by GENCPM at build time (build.sh).
;
;	GENCPM sizes each drive's allocation and checksum vectors from the
;	DPB its DPH points at when it runs: DPBMAX, as large as any format
;	needs. SELDSK puts the real DPB in when the BDOS logs the drive in.
;	When another image is mounted later (SET-UP's Drives panel), READ
;	and WRITE see port 8's format or mount count differ and report a
;	media change, and the BDOS logs the drive in again. The DPHs and SELDSK are in the banked
;	segment (bank 0, where the BDOS runs), so the common part stays
;	small (TPA 61K, as with picosim's BIOS); the DPBs must be common.
;
;	Character devices (bd qtq): TTY1 the screen and keyboard (SIO1),
;	TTY2 the board's serial port (SIO2, ports 2/3), AUXIN:/AUXOUT: by
;	default; DEVICE moves CON: and AUX: between them. TTY2's rate can
;	be set (DEVICE TTY2 [9600], bd c2d) where the machine has port 0EH:
;	BOOT marks it soft and puts the rate in use in CHRTBL, DEVINI sends
;	DEVICE's code and reads back what was done.
;
; History:
; 03-JUL-2024 first public release
; 07-JUL-2024 added RTC
; 14-JUL-2024 fixed bug, FCB one byte short
; 23-JUL-2024 fixed status bug in READ/WRITE found by Thomas
; 13-OCT-2026 drives A-H, formats from port 8 (dvi-feather-terminal)
;
WARM	EQU	0		; BIOS warm start
BDOS	EQU	5		; BDOS entry
;
; 	BDOS functions
;
PRINT	EQU	9		; print string
OPEN	EQU	15		; open file
READS	EQU	20		; read sequential
DMA	EQU	26		; setup DMA
MULTI	EQU	44		; multi sector I/O
;
;	RTC commands
;
GETSEC	EQU	0		; get seconds from RTC
GETMIN	EQU	1		; get minutes from RTC
GETHOU	EQU	2		; get hours from RTC
GETDAL	EQU	3		; get days low from RTC
GETDAH	EQU	4		; get days high from RTC
;
;	character devices mode byte
;
mb$in	EQU	00000001B	; device may do input
mb$out	EQU	00000010B	; device may do output
mb$inou	EQU	mb$in+mb$out	; device may do both
mb$soft	EQU	00000100B	; software selectable baud rate
mb$ser	EQU	00001000B	; device may use protocol
mb$xon	EQU	00010000B	; XON/XOFF protocol enabled
;
;	character devices baud rate byte
;
baud$0	EQU	0		; Altair 88-SIO baudrate not software selectable
;
TPA	EQU	0100H		; start of TPA
;
;	I/O ports
;
TTY1	EQU	01H		; tty 1 data
TTY1S	EQU	00H		; tty 1 status
TTY2	EQU	03H		; tty 2 data: SIO2, the board's serial port
TTY2S	EQU	02H		; tty 2 status
FDC	EQU	04H		; FDC
DINFO	EQU	08H		; drive format: OUT drive, IN 0 none, 1 8" floppy,
				; 2 4 MB, 3 8 MB hard disk (ours)
RATE	EQU	0EH		; tty 2's rate as a baud code: IN the one in use
				; (0 none, 0FFH no such port), OUT one (ours)
MMUSEL	EQU	40H		; MMU bank select
CLKCMD	EQU	41H		; RTC command
CLKDAT	EQU	42H		; RTC data
LEDS	EQU	0FFH		; frontpanel LED's
;
;	external references in SCB
;
	EXTRN	@civec, @covec, @aovec, @aivec, @lovec, @bnkbf
	EXTRN	@crdma, @crdsk, @fx, @resel, @vinfo, @usrcd
	EXTRN	@ermde, @date, @hour, @min, @sec, @mxtpa
;
NDRV	EQU	8		; drives A-H
;
	CSEG
;
;	jump vector for individual subroutines
;
	JMP	BOOT		; perform cold start initialization
WBOOTE:	JMP	WBOOT		; perform warm start initialization
	JMP	CONST		; check for console input char ready
	JMP	CONIN		; read console character in
	JMP	CONOUT		; write console character out
	JMP	LIST		; write list character out
	JMP	AUXOUT		; write auxiliary character out
	JMP	AUXIN		; read auxiliary character in
	JMP	HOME		; move head to track 0 on selected disk
	JMP	SELDSK		; select disk drive
	JMP	SETTRK		; set track number
	JMP	SETSEC		; set sector number
	JMP	SETDMA		; set DMA address
	JMP	READ		; read specified sector
	JMP	WRITE		; write specified sector
	JMP	LISTST		; return list status
	JMP	SECTRAN		; translate logical to physical sector
	JMP	CONOST		; return output status of console
	JMP	AUXIST		; return input status of auxiliary
	JMP	AUXOST		; return output status of auxiliary
	JMP	DEVTBL		; return address of character I/O table
	JMP	DEVINI		; initialize character I/O devices
	JMP	DRVTBL		; return address of disk drive table
	JMP	MULTIO		; set number of sectors to read/write
	JMP	FLUSH		; flush deblocking buffers
	JMP	MOVE		; memory to memory move
	JMP	TIME		; time set/get signal
	JMP	SELMEM		; select bank of memory
	JMP	SETBNK		; specify bank for DMA operation
	JMP	XMOVE		; set bank for memory DMA transfer
	JMP	0		; reserved for system implementer
	JMP	0		; reserved for future use
	JMP	0		; reserved for future use
;
;	drive table
;
DRIVES:	DW	DPH0
	DW	DPH1
	DW	DPH2
	DW	DPH3
	DW	DPH4
	DW	DPH5
	DW	DPH6
	DW	DPH7
	DW	0
	DW	0
	DW	0
	DW	0
	DW	0
	DW	0
	DW	0
	DW	0
;
;	disk parameter blocks, as src/machine/cpm/bios.asm's, plus CP/M 3's
;	physical sector size and shift (128 bytes: 0, 0). Hard disks have no
;	directory checksums, but are not marked permanent (8000H): the BDOS
;	ignores a media change (READ's 0FFH) on a permanent drive, and an
;	image of another format can be mounted on any drive. In common
;	memory: BDOS function 31 gives programs (SHOW) the DPB's address.
;
;	IBM 3740 8" SD disk
DPBFD:	DW	26		; sectors per track
	DB	3		; block shift factor
	DB	7		; block mask
	DB	0		; extend mask
	DW	242		; disk size - 1
	DW	63		; directory max
	DB	192		; alloc 0
	DB	0		; alloc 1
	DW	16		; check size
	DW	2		; track offset
	DB	0,0		; physical sector size and shift
;
;	z80pack 4 MB hard disk: 255 tracks of 128 sectors, 2 KB blocks
DPBHD4:	DW	128		; sectors per track
	DB	4		; block shift factor
	DB	15		; block mask
	DB	0		; extend mask
	DW	2039		; disk size - 1
	DW	1023		; directory max
	DB	255		; alloc 0
	DB	255		; alloc 1
	DW	0		; check size: none
	DW	0		; track offset
	DB	0,0		; physical sector size and shift
;
;	8 MB hard disk: 256 tracks of 255 sectors, 4 KB blocks
DPBHD8:	DW	255		; sectors per track
	DB	5		; block shift factor
	DB	31		; block mask
	DB	1		; extend mask
	DW	2039		; disk size - 1
	DW	1023		; directory max
	DB	255		; alloc 0
	DB	0		; alloc 1
	DW	0		; check size: none
	DW	0		; track offset
	DB	0,0		; physical sector size and shift
;
;	FDC command bytes
;
CMD:	DS	4
DDTRK	EQU	CMD+0		; track
DDSEC	EQU	CMD+1		; sector
DDLDMA	EQU	CMD+2		; DMA address low
DDHDMA	EQU	CMD+3		; DMA address high
;
;	character device table
;
CHRTBL:	DB	'TTY1  '	; device 0 (vector bit 15): the screen and keyboard
	DB	mb$inou+mb$ser
	DB	baud$0
	DB	'TTY2  '	; device 1 (vector bit 14): the serial port
	DB	mb$inou+mb$ser	; mb$soft and the rate's code set by BOOT
	DB	baud$0
;
	DB	0
;
;	FCB for loading CCP
;
CCPFCB:	DB	1,'CCP     COM',0,0,0,0
	DB	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
CCPCR:	DB	0
CCPREC:	DB	0,0,0
;
;	BIOS error messages
;
OPNERR:	DB	'Open error CCP.COM',13,10,'$'
RDRERR:	DB	'Read error CCP.COM',13,10,'$'
;
BANK:	DB	0		; bank to select for DMA
SDISK:	DB	0		; selected disk
FMTS:	DB	0,0,0,0,0,0,0,0	; the format each drive was logged in with
MNTS:	DB	0,0,0,0,0,0,0,0	; and the mount count (port 8) then
;
	DS	32		; small stack
STACK:
;
	DSEG
;
SIGNON:	DB	13,10
	DB	'Banked BIOS V1.3, drives A-H',13,10
	DB	'Copyright (C) 2024 Udo Munk',13,10,13,10
	DB	0
;
;	disk parameter headers: until a drive is selected, DPBMAX (GENCPM
;	sizes the vectors from it)
;
DPH0:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH1:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH2:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH3:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH4:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH5:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH6:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
DPH7:	DW	TRANS		; sector translate table
	DB	0,0,0,0		; BDOS scratch area
	DB	0,0,0,0,0
	DB	0		; media flag
	DW	DPBMAX		; disk parameter block
	DW	0FFFEH		; checksum vector
	DW	0FFFEH		; allocation vector
	DW	0FFFEH		; directory buffer control block
	DW	0FFFFH		; DTABCB not used
	DW	0FFFFH		; hashing not used
	DB	0		; hash bank
;
;	sector translate table for IBM 3740 8" SD disk
;
TRANS:	DB	1,7,13,19	; sectors 1,2,3,4
	DB	25,5,11,17	; sectors 5,6,7,8
	DB	23,3,9,15	; sectors 9,10,11,12
	DB	21,2,8,14	; sectors 13,14,15,16
	DB	20,26,6,12	; sectors 17,18,19,20
	DB	18,24,4,10	; sectors 21,22,23,24
	DB	16,22		; sectors 25,26
;
;	for GENCPM only: the most blocks, directory entries and checksums
;	of any format
DPBMAX:	DW	255		; sectors per track
	DB	5		; block shift factor
	DB	31		; block mask
	DB	1		; extend mask
	DW	2039		; disk size - 1
	DW	1023		; directory max
	DB	255		; alloc 0
	DB	0		; alloc 1
	DW	16		; check size
	DW	0		; track offset
	DB	0,0		; physical sector size and shift
;
DPBTAB:	DW	DPBFD,DPBHD4,DPBHD8
;
;	get control from cold start loader
;	and initialize system
;
BOOT:	LXI	SP,STACK
;
	LXI	H,8000H		; device 0
	SHLD	@civec		; CONIN:=TTY1
	SHLD	@covec		; CONOUT:=TTY1
	MVI	H,40H		; device 1
	SHLD	@aivec		; AUXIN:=TTY2
	SHLD	@aovec		; AUXOUT:=TTY2
	MVI	H,0
	SHLD	@lovec		; LST:=NULL
;
	IN	RATE		; tty 2's rate, if the machine can change it
	CPI	0FFH
	JZ	BOOT0		; no: a fixed rate, as picosim's
	STA	CHRTBL+15	; its baud code (0: none, e.g. 115200)
	LDA	CHRTBL+14
	ORI	mb$soft		; DEVICE may set it
	STA	CHRTBL+14
BOOT0:
;
	MVI	A,10H		; setup FDC command
	OUT	FDC
	LXI	H,CMD
	MOV	A,L
	OUT	FDC
	MOV	A,H
	OUT	FDC
;
	LXI	H,SIGNON	; print signon
BOOT1:	MOV	A,M		; get next message bye
	ORA	A		; is it zero ?
	JZ	BOOT2		; yes, done
	MOV	C,A		; no, print character on console
	PUSH	H
	CALL	CONOUT
	POP	H
	INX	H		; and do next
	JMP	BOOT1
;
;
;	SETDEF's defaults (bd f8w.9), in the SCB from its @CIVEC at +22H
;	(RMAC takes external + constant, but no EQU of one): programs found
;	on the default drive, then A:, then B:; COM before SUB; temporary
;	files on B: (CPM32, an 8 MB disk); no program name display.
;	PROFILE.SUB can change them (README.TXT on CPM31).
BOOT2:	LXI	H,@civec+(4CH-22H) ; drive search chain, 0 the default
	MVI	M,0		; default drive
	INX	H
	MVI	M,1		; A:
	INX	H
	MVI	M,2		; B:
	INX	H
	MVI	M,0FFH		; end of the chain
	MVI	A,2		; temporary file drive (SCB+50H): B:
	STA	@civec+(50H-22H)
	LDA	@civec+(18H-22H) ; CCP flags: bits 3-4 order, 0-1 display
	ANI	0E4H		; no display, order bits clear (COM)
	ORI	08H		; order COM, SUB
	STA	@civec+(18H-22H)
	JMP	WBOOT
;
	CSEG
;
;	get control when a warm start occurs
;
WBOOT:	LXI	SP,STACK
	MVI	A,1		; select memory bank 1
	CALL	SELMEM
;
	MVI	A,0C3H		; JMP instruction
	STA	WARM
	LXI	H,WBOOTE	; warm boot entry point
	SHLD	WARM+1
	STA	BDOS
	LHLD	@mxtpa		; BDOS entry point
	SHLD	BDOS+1
;
	XRA	A		; initialize FCB for CCP
	STA	CCPFCB+12
	STA	CCPFCB+15
	STA	CCPCR
	STA	CCPREC
	STA	CCPREC+1
	STA	CCPREC+2
	LXI	D,CCPFCB	; open file CCP.COM
	MVI	C,OPEN
	CALL	BDOS
	LXI	D,OPNERR
	INR	A
	JZ	CPPERR		; file open error
	LXI	D,TPA		; setup DMA to TPA
	MVI	C,DMA
	CALL	BDOS
	LXI	D,128		; read up to 16 KB
	MVI	C,MULTI
	CALL	BDOS
	LXI	D,CCPFCB	; read the CCP into memory
	MVI	C,READS
	CALL	BDOS
	LXI	D,RDRERR
	INR	A
	JNZ	TPA		; start CCP
;
CPPERR:	MVI	C,PRINT		; error loading CCP, print error message
	CALL	BDOS
	HLT			; and halt the machine
;
;	The BIOS entries only CP/M 3 has (DEVTBL, DEVINI, DRVTBL, TIME)
;	are banked: the BDOS calls them, function 50 too, with bank 0 in
;	place, and the common part must stay within 3 pages (TPA 61K).
;
	DSEG
;
;	I/O drivers for character devices
;
DEVTBL:	LXI	H,CHRTBL	; return address of character I/O table (common)
	RET
;
;	initialize character I/O devices
;
DEVINI:	MOV	A,C		; device number
	CPI	1		; tty 2 is the only one with a rate to set
	RNZ
	LDA	CHRTBL+15	; the baud code DEVICE put there
	OUT	RATE		; tty 2 at that rate, until reset or SET-UP's save
	IN	RATE		; the rate in use: unchanged if it could not be had
	CPI	0FFH
	RZ
	STA	CHRTBL+15
	RET
;
;	I/O drivers for disks
;
DRVTBL:	LXI	H,DRIVES	; return address of disk drive table
	RET
;
	CSEG
;
;	The logical devices: CON: and AUX: go to every device their vector
;	selects, TTY1 (bit 15) and TTY2 (bit 14), so DEVICE can move or
;	copy them (DEVICE CONOUT:=TTY1,TTY2).
;
CONST:	LHLD	@civec		; console input status
	JMP	VECIST
CONIN:	LHLD	@civec		; console input
	JMP	VECIN
CONOST:	LHLD	@covec		; console output status
	JMP	VECOST
CONOUT:	LHLD	@covec		; console output
	JMP	VECOUT
AUXIST:	LHLD	@aivec		; auxiliary input status
	JMP	VECIST
AUXIN:	LHLD	@aivec		; auxiliary input
	JMP	VECIN
AUXOST:	LHLD	@aovec		; auxiliary output status
	JMP	VECOST
AUXOUT:	LHLD	@aovec		; auxiliary output
	JMP	VECOUT
;
;	list output status
;
LISTST:	JMP	DEVNRY		; no printer, never ready
;
;	list output
;
LIST:	RET			; no printer
;
;	input status of the devices vector HL selects: 0FFH if one has a
;	character
;
VECIST:	MOV	A,H
	ANI	80H		; device 0?
	JZ	VIS1
	CALL	TTY1IS
	JZ	DEVRDY
VIS1:	MOV	A,H
	ANI	40H		; device 1?
	JZ	DEVNRY
	CALL	TTY2IS
	JZ	DEVRDY
	JMP	DEVNRY
;
;	input from the first of them with a character, waiting for one;
;	none selected: CTL-Z
;
VECIN:	MOV	A,H
	ANI	0C0H		; any device?
	MVI	A,01AH
	RZ
	MOV	A,H
	ANI	80H		; device 0?
	JZ	VIN1
	CALL	TTY1IS
	JZ	TTY1IN
VIN1:	MOV	A,H
	ANI	40H		; device 1?
	JZ	VECIN
	CALL	TTY2IS
	JZ	TTY2IN
	JMP	VECIN
;
;	output status: 0FFH if all of them can take a character
;
VECOST:	MOV	A,H
	ANI	80H		; device 0?
	JZ	VOS1
	CALL	TTY1OS
	JNZ	DEVNRY
VOS1:	MOV	A,H
	ANI	40H		; device 1?
	JZ	DEVRDY
	CALL	TTY2OS
	JNZ	DEVNRY
	JMP	DEVRDY
;
;	output C to all of them
;
VECOUT:	MOV	A,H
	ANI	80H		; device 0?
	CNZ	TTY1OU
	MOV	A,H
	ANI	40H		; device 1?
	RZ
	JMP	TTY2OU
;
;	tty 1 input status
;
TTY1IS:	IN	TTY1S		; get status
	ANI	01H		; mask bit
	RET
;
;	tty 1 input
;
TTY1IN:	IN	TTY1		; get character
	RET
;
;	tty 1 output status
;
TTY1OS:	IN	TTY1S		; get status
	ANI	80H		; mask bit
	RET
;
;	tty 1 output
;
TTY1OU:	IN	TTY1S		; get status
	RLC			; test bit 7
	JC	TTY1OU		; wait until transmitter ready
	MOV	A,C		; get character to A
	OUT	TTY1		; send to tty 1
	RET
;
;	tty 2 input status
;
TTY2IS:	IN	TTY2S		; get status
	ANI	01H		; mask bit
	RET
;
;	tty 2 input
;
TTY2IN:	IN	TTY2		; get character
	RET
;
;	tty 2 output status
;
TTY2OS:	IN	TTY2S		; get status
	ANI	80H		; mask bit
	RET
;
;	tty 2 output: waits while the board's transmit queue is full
;
TTY2OU:	IN	TTY2S		; get status
	RLC			; test bit 7
	JC	TTY2OU		; wait until transmitter ready
	MOV	A,C		; get character to A
	OUT	TTY2		; send to tty 2
	RET
;
;	return with device ready
;
DEVRDY:	MVI	A,0FFH
	RET
;
;	return with device not ready
;
DEVNRY:	XRA	A
	RET
;
	DSEG
;
;	move to track 0 position of current drive
;
HOME:	MVI	C,0		; select track 0
	JMP	SETTRK
;
;	select disk given by register C (0-7); bit 0 of E is 0 on its
;	first select since the drive was reset (log-in). Port 8 gives the
;	format of the image mounted on it: none (or unknown) is a select
;	error. On a log-in the DPH gets the format's translate table and
;	DPB. Another image mounted later shows in READ and WRITE (CHKFMT).
;
SELDSK:	LXI	H,0		; HL = error return code
	MOV	A,C		; get disk # to A
	CPI	NDRV		; one of ours?
	RNC			; no, return with error
	OUT	DINFO		; ask the controller for its format
	IN	DINFO
	ORA	A		; no disk?
	RZ			; return with error
	CPI	4		; known format?
	RNC			; no, return with error
	MOV	D,A		; D = format 1-3
	MOV	A,C
	STA	SDISK
	MOV	A,E
	RAR			; selected before?
	JC	DPHOF		; yes: HL = the DPH as it is
	MVI	B,0		; FMTS(disk) = the format logged in
	LXI	H,FMTS
	DAD	B
	MOV	M,D
	LXI	H,MNTS		; MNTS(disk) = its mount count
	DAD	B
	IN	DINFO
	MOV	M,A
	CALL	DPHOF		; HL = the DPH
	PUSH	H		; returned
	LXI	B,TRANS		; floppies are skewed
	MOV	A,D
	CPI	1
	JZ	SELD1
	LXI	B,0		; hard disks are not
SELD1:	MOV	M,C		; translate table
	INX	H
	MOV	M,B
	LXI	B,11		; .DPH+12 = DPB
	DAD	B
	MOV	A,D		; BC = DPBTAB(format - 1)
	DCR	A
	ADD	A
	MOV	C,A
	MVI	B,0
	PUSH	H
	LXI	H,DPBTAB
	DAD	B
	MOV	C,M
	INX	H
	MOV	B,M
	POP	H
	MOV	M,C		; DPB
	INX	H
	MOV	M,B
	POP	H
	RET
;
;	HL = the DPH of drive SDISK (uses BC)
;
DPHOF:	LDA	SDISK
	ADD	A
	MOV	C,A
	MVI	B,0
	LXI	H,DRIVES
	DAD	B
	MOV	A,M
	INX	H
	MOV	H,M
	MOV	L,A
	RET
;
	CSEG
;
;	set track given by register C
;
SETTRK:	MOV	A,C		; get track to A
	STA	DDTRK		; and set in FDC command bytes
	RET
;
;	set sector given by register C
;
SETSEC:	MOV	A,C		; get sector to A
	STA	DDSEC		; and set in FDC command bytes
	RET
;
	DSEG
;
;	translate the sector given by BC using
;	the translation table given by DE
;
SECTRAN:MOV	A,D		; do we have a translation table ?
	ORA	E
	JNZ	SECT1		; yes, translate
	MOV	L,C		; no, return untranslated
	MOV	H,B
	INR	L		; sector no. start with 1
	RET
SECT1:	XCHG			; HL = .TRANS
	DAD	B		; HL = .TRANS(sector)
	XCHG
	LDAX	D
	MOV	L,A		; L = TRANS(sector)
	MVI	H,0		; HL = TRANS(sector)
	RET			; with value in HL
;
	CSEG
;
;	set DMA address given by register BC
;
SETDMA:	MOV	A,C		; low order address
	STA	DDLDMA		; set in FDC command bytes
	MOV	A,B		; high order address
	STA	DDHDMA		; set in FDC command bytes
	RET
;
;	perform read operation
;
READ:	CALL	CHKFMT		; another image mounted?
	RNZ			; yes, A = 0FFH: media changed
	LDA	BANK		; switch to saved bank
	OUT	MMUSEL
	LDA	SDISK		; get disk
	ORI	20H		; mask in read command
	OUT	FDC		; ask FDC to execute the command
	XRA	A		; reselect bank 0
	OUT	MMUSEL
	IN	FDC		; get FDC status
	ORA	A		; is it zero ?
	RZ			; return if OK
	CMA			; complement for LED's
	OUT	LEDS		; display the error code
	MVI	A,1		; nonrecoverable error
	RET			; return with error
;
;	perform write operation
;
WRITE:	CALL	CHKFMT		; another image mounted?
	RNZ			; yes, A = 0FFH: media changed
	LDA	BANK		; switch to saved bank
	OUT	MMUSEL
	LDA	SDISK		; get disk
	ORI	40H		; mask in write command
	OUT	FDC		; ask FDC to execute the command
	XRA	A		; reselect bank 0
	OUT	MMUSEL
	IN	FDC		; get FDC status
	ORA	A		; is it zero ?
	RZ			; return if OK
	CMA			; complement for LED's
	OUT	LEDS		; display the error code
	MVI	A,1		; nonrecoverable error
	RET			; return with error
;
;	Z if the selected drive still has the image it was logged in with
;	(port 8: the same format and mount count), else NZ and A = 0FFH:
;	the BDOS logs it in again (another image mounted, or none).
;
CHKFMT:	LDA	SDISK
	MOV	C,A
	OUT	DINFO
	MVI	B,0
	LXI	H,FMTS
	DAD	B
	IN	DINFO		; the format
	CMP	M
	JNZ	CHKF1
	LXI	H,MNTS
	DAD	B
	IN	DINFO		; the mount count
	CMP	M
	RZ
CHKF1:	MVI	A,0FFH
	ORA	A
	RET
;
;	set count of consecutive sectors
;	for read or write
;
MULTIO:	XRA	A		; do nothing
	RET
;
;	force physical buffer flushing
;
FLUSH:	XRA	A		; no user deblocking
	RET
;
;	memory to memory block move
;	HL = destination address
;	DE = source address
;	BC = count
;
MOVE:	LDAX	D
	MOV	M,A
	INX	D
	INX	H
	DCX	B
	MOV	A,B
	ORA	C
	JNZ	MOVE
	RET
;
;	select memory bank
;
SELMEM:	OUT	MMUSEL
	RET
;
;	specify memory bank for DMA operation
;
SETBNK:	STA	BANK
	RET
;
;	set banks for following MOVE
;
XMOVE: RET			; no memory to memory DMA available
;
	DSEG			; banked, as DEVTBL above
;
;	get/set time: C = 0FFH sets the clock from the SCB (the machine's
;	clock ports take writes, src/machine/machine.h): the day number,
;	low byte first (the high byte's write sets it), then hours,
;	minutes, seconds in BCD
;
TIME:	MOV	A,C
	CPI	0FFH
	JNZ	GETTIM
	MVI	A,GETDAL
	OUT	CLKCMD
	LDA	@date
	OUT	CLKDAT
	MVI	A,GETDAH
	OUT	CLKCMD
	LDA	@date+1
	OUT	CLKDAT
	MVI	A,GETHOU
	OUT	CLKCMD
	LDA	@hour
	OUT	CLKDAT
	MVI	A,GETMIN
	OUT	CLKCMD
	LDA	@min
	OUT	CLKDAT
	MVI	A,GETSEC
	OUT	CLKCMD
	LDA	@sec
	OUT	CLKDAT
	RET
GETTIM:	MVI	A,GETSEC	; get seconds
	OUT	CLKCMD
	IN	CLKDAT
	STA	@sec
	MVI	A,GETMIN	; get minutes
	OUT	CLKCMD
	IN	CLKDAT
	STA	@min
	MVI	A,GETHOU	; get hours
	OUT	CLKCMD
	IN	CLKDAT
	STA	@hour
	MVI	A,GETDAL	; get day
	OUT	CLKCMD
	IN	CLKDAT
	STA	@date
	MVI	A,GETDAH
	OUT	CLKCMD
	IN	CLKDAT
	STA	@date+1
	RET
;
	END			; of BIOS
