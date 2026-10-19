\ VIEW from F83's own drive                          18Oct26 dvt
This file patches F83.COM so that VIEW and FIX find the
system's source on the drive F83.COM was loaded from, not on
the current drive. At every start VIEW-DRIVE puts that drive
into the drive byte of each file in VIEW-FILES.

CP/M 3 keeps the drive a program was loaded from at 0050H
(1 = A:), also when the search path found it, so  A>F83  with
F83 on B: still views B:KERNEL80.BLK. CP/M 2.2 does not: there
the files stay on the current drive, as before, and F83 should
be started from its own drive (B:  then  F83).

The built-in F83 disk's F83.COM is the original with this file
loaded. To patch a newly made F83.COM the same way:
   F83 VIEWDRV.BLK   then   1 LOAD

---
\ VIEW from F83's own drive                          18Oct26 dvt
ONLY FORTH DEFINITIONS ALSO DOS
6 VIEWS VIEWDRV.BLK   6 VIEW# !   ( this file is view file 6 )
: LOAD-DRIVE   (S -- n )   ( 0050H under CP/M 3, else 0 )
   0 12 BDOS 48 < IF  0 EXIT  THEN   80 C@  DUP 17 < AND  ;
: VIEW-DRIVE   (S -- )   LOAD-DRIVE   16 0 DO
   VIEW-FILES I 2* + @  ?DUP IF  >BODY  OVER SWAP C!  THEN
   LOOP  DROP  ;
: HELLO-VIEW   (S -- )   VIEW-DRIVE  HELLO  ;
' HELLO-VIEW IS BOOT
MARK EMPTY     HERE FENCE !
SAVE-SYSTEM F83.COM
CR .( F83.COM saved: VIEW uses the drive it was loaded from. )
