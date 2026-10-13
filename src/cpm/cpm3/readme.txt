CP/M 3 on the DVI terminal's Z80 machine
========================================

Boot with CPM31 on A: and CPM32 (an 8 MB disk) on B:. The system starts
with these SETDEF settings, set by the BIOS at every cold boot:

  Drive search path   default drive, then A:, then B:
  Search order        COM, then SUB
  Temporary drive     B: (SUBMIT's and other programs' work files)
  Program name        not displayed

so programs on A: and B: run from any drive, and temporary files go to
the large B: rather than the nearly full A:. Type SETDEF alone to see
the settings in use.

Changing the defaults
---------------------

SETDEF changes a setting until the next cold boot (^C keeps it):

  SETDEF *,A:,B:,C:          search C: as well
  SETDEF [TEMPORARY=E:]      temporary files on E:
  SETDEF [ORDER=(SUB,COM)]   SUB files before COM files
  SETDEF [DISPLAY]           show where each program was found

To make a change last, put the SETDEF line in PROFILE.SUB on A:, which
CP/M 3 runs after every cold boot (it already loads VT100DYN, which sets
the console's size from the terminal). Edit it with ED:

  A>ED PROFILE.SUB
       : *#A                 read the whole file
      1: *B#T                show it
      1:  vt100dyn
      1: *BI                 insert at the start: type the line, Return,
      1:  SETDEF [TEMPORARY=E:]
      2:  ^Z                 and ^Z to stop inserting
      2: *E                  save and leave

(ED numbers the lines; you type what follows the * or, while inserting,
the line number.)

or with WM (WordMaster, on B:): WM PROFILE.SUB. Remove the line again to
go back to the BIOS's defaults. HELP SETDEF tells the rest.
