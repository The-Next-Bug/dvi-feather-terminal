\ AY-3-8910 sound chip words                        26Oct26 dvt
Words for the AY-3-8910 on ports D8h (register) / D0h (data),
as on the RC2014's sound board, and examples that play it.

   F83 AY.BLK   1 LOAD   then   SCALE   or   AY? .

Screens 2 and 3 are the words, 4 to 6 the examples; each
screen's shadow (the screen number + 7) explains it.
Channels are 0 (A), 1 (B) and 2 (C).
---
\ AY-3-8910 words   Load screen                    26Oct26 dvt
ONLY FORTH ALSO DOS   7 VIEWS AY.BLK   7 VIEW# !
ONLY FORTH ALSO DEFINITIONS   DECIMAL
1 5 +THRU
CR .( AY words loaded: SCALE CHORDS SIREN DRUM PAN TUNE )
---
\ AY-3-8910: the chip                              26Oct26 dvt
216 CONSTANT AY-SELECT   208 CONSTANT AY-DATA
: AY!   (S b reg -- )   AY-SELECT PC!  AY-DATA PC!  ;
: AY@   (S reg -- b )   AY-SELECT PC!  AY-DATA PC@  ;
: AY?   (S -- f )   255 1 AY!  1 AY@  15 =  ;
: TONE   (S period ch -- )   2* >R  0 256 UM/MOD SWAP
   R@ AY!  R> 1+ AY!  ;
: VOLUME   (S level ch -- )   8 + AY!  ;
: NOISE   (S period -- )   6 AY!  ;
: VOICES   (S tones noises -- )   8 * OR  63 XOR  7 AY!  ;
: ENVELOPE   (S period shape -- )   SWAP 0 256 UM/MOD SWAP
   11 AY!  12 AY!  13 AY!  ;
: QUIET   (S -- )   14 0 DO  0 I AY!  LOOP  63 7 AY!  ;
: STEREO   (S n -- )   16 AY!  ;
---
\ AY-3-8910: notes and time                        26Oct26 dvt
: HZ>PERIOD   (S hz -- period )   >R 115200. R> UM/MOD NIP  ;
CREATE OCTAVE2   ( C2 to B2 )
   1761 , 1662 , 1569 , 1481 , 1398 , 1319 ,
   1245 , 1175 , 1110 , 1047 ,  988 ,  933 ,
: NOTE>PERIOD   (S n -- period )
   12 /MOD SWAP 2* OCTAVE2 + @  SWAP 2- 0 MAX 0 ?DO 2/ LOOP  ;
: NOTE   (S n ch -- )   >R NOTE>PERIOD R> TONE  ;
: TICKS   (S -- ms )   68 PC@  69 PC@ 256 * +  ;
: WAIT   (S ms -- )   TICKS +
   BEGIN  DUP TICKS - 0>  WHILE  REPEAT  DROP  ;
---
\ AY examples: SCALE CHORDS SIREN                  26Oct26 dvt
CREATE MAJOR   0 C, 2 C, 4 C, 5 C, 7 C, 9 C, 11 C, 12 C,
: SCALE   (S -- )   QUIET  1 0 VOICES  13 0 VOLUME
   8 0 DO  MAJOR I + C@ 48 + 0 NOTE  300 WAIT  LOOP  QUIET  ;
: CHORD   (S root -- )   DUP 0 NOTE  DUP 4 + 1 NOTE  7 + 2 NOTE
   ;
: CHORDS   (S -- )   QUIET  7 0 VOICES
   3 0 DO  11 I VOLUME  LOOP
   48 CHORD 700 WAIT  53 CHORD 700 WAIT  55 CHORD 700 WAIT
   60 CHORD 1400 WAIT  QUIET  ;
: SIREN   (S -- )   QUIET  1 0 VOICES  12 0 VOLUME
   3 0 DO  100 400 DO  I 0 TONE  3 WAIT  -4 +LOOP
           400 100 DO  I 0 TONE  3 WAIT   4 +LOOP  LOOP
   QUIET  ;
---
\ AY examples: DRUM PAN                            26Oct26 dvt
: HIT   (S noise -- )   NOISE  16 2 VOLUME  1500 0 ENVELOPE  ;
: DRUM   (S -- )   QUIET  0 4 VOICES
   4 0 DO  2 HIT 250 WAIT  2 HIT 250 WAIT  20 HIT 500 WAIT
   LOOP  QUIET  ;
CREATE BITS   1 C, 2 C, 4 C,
: .LAYOUT   (S n -- )   DUP 1 = IF ." mono " THEN
   DUP 2 = IF ." ABC " THEN   3 = IF ." ACB " THEN  ;
: PAN   (S -- )
   4 1 DO  I STEREO  CR I .LAYOUT
      3 0 DO  QUIET  BITS I + C@ 0 VOICES  262 I TONE
         13 I VOLUME  ASCII A I + EMIT SPACE  500 WAIT  LOOP
   LOOP  QUIET  0 STEREO  ;
---
\ AY examples: TUNE from a table                   26Oct26 dvt
CREATE KOROBEINIKI
   64 C, 2 C,  59 C, 1 C,  60 C, 1 C,  62 C, 2 C,  60 C, 1 C,
   59 C, 1 C,  57 C, 2 C,  57 C, 1 C,  60 C, 1 C,  64 C, 2 C,
   62 C, 1 C,  60 C, 1 C,  59 C, 3 C,  60 C, 1 C,  62 C, 2 C,
   64 C, 2 C,  60 C, 2 C,  57 C, 2 C,  57 C, 4 C,  0 C, 0 C,
200 CONSTANT EIGHTH
: PLAY   (S addr -- )
   QUIET  1 0 VOICES
   BEGIN  DUP C@  OVER 1+ C@  DUP  WHILE   SWAP ?DUP
      IF  0 NOTE  16 0 VOLUME  2880 0 ENVELOPE
      ELSE  0 0 VOLUME  THEN   EIGHTH * WAIT  2+
   REPEAT  2DROP  QUIET  ;
: TUNE   (S -- )   KOROBEINIKI PLAY  ;
===
\ AY-3-8910 sound chip words                        26Oct26 dvt
The chip is on the Z80 machine of the DVI terminal firmware
(src/machine/machine.h), clocked at 1.8432 MHz like RC2014's.
A tone of f Hz has period 115200 / f.

The firmware's own register 16, STEREO, picks where A, B and
C are heard; a real AY ignores it, and reads 255 there.

Words are in FORTH; VIEW shows their source (view file 7).
---
\ Load screen                                       26Oct26 dvt
VIEWS makes AY.BLK view file 7, so VIEW finds these words.
1 5 +THRU loads screens 2 to 6: 1 2 +THRU for the words alone
(then  4 LOAD  and so on for an example).
---
\ The chip                                          26Oct26 dvt
AY!   write b into register reg.   AY@   read register reg.
AY?   true if the chip is there: register 1 keeps 4 bits.
TONE   channel ch's tone period, 1 to 4095 (registers 0-5).
VOLUME   channel ch's level 0-15, or 16: the envelope's.
NOISE   the noise period, 0 to 31.
VOICES   which channels sound: bits 0 A, 1 B, 2 C, for tones
   and for noise.   7 0 VOICES  all tones, no noise.
ENVELOPE   its period (a ramp: 256 x period clocks) and shape
   0-15 (a write starts it; 0 decays once, 8 repeats, ...).
QUIET   every register 0, everything off; STEREO left alone.
STEREO   the layout: 0 SET-UP's setting, 1 mono, 2 ABC (A left,
   B center, C right), 3 ACB (A left, C center, B right).
   16 AY@  reads it back: bits 0-1 as written, bits 4-5 the
   layout in use (1 mono, 2 ABC, 3 ACB).
---
\ Notes and time                                    26Oct26 dvt
HZ>PERIOD   the tone period for hz (2 and up).
OCTAVE2   the periods of C2 to B2; each octave up halves them.
NOTE>PERIOD   note n's period: n = octave x 12 + semitone,
   C = 0 to B = 11, so 48 is C4 and 57 A4.
NOTE   note n on channel ch.
TICKS   the firmware's millisecond count (ports 68 and 69; the
   low byte read first), 16 bits, wrapping.
WAIT   ms milliseconds by TICKS: the Z80 has no fixed speed.
---
\ Examples: SCALE CHORDS SIREN                      26Oct26 dvt
SCALE   C4 to C5 on A, 300 ms a note.
CHORD   the major chord on root: A the root, B the third, C
   the fifth.
CHORDS   C, F, G and C major on A, B and C.
SIREN   A swept down and up three times.
---
\ Examples: DRUM PAN                                26Oct26 dvt
HIT   a noise burst on C through the decaying envelope.
DRUM   four bars of a low and a high hit.
.LAYOUT   a layout's name.
PAN   A, B and C in turn in each layout (mono, ABC, ACB), then
   SET-UP's again: with ABC, A left, B center and C right.
---
\ Examples: TUNE from a table                       26Oct26 dvt
KOROBEINIKI   pairs of a note (as NOTE takes it; 0 a rest) and
   its length in eighths; 0 0 ends it. A 19th century Russian
   folk song (public domain), the Tetris theme.
EIGHTH   ms an eighth: 150 beats a minute.
PLAY   a table on A, each note a decay of the envelope.
TUNE   plays KOROBEINIKI.
