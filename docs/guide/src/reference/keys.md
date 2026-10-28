# Keys sent

What the USB keyboard's keys send (`src/input/keyboard.c`), by terminal
type and mode. SET-UP and the CP/M monitor always get the VT100 ANSI-mode
keys.

## Characters

- Letters and the main keys: US layout; Shift and Caps Lock as usual.
- **Ctrl** with a letter or `@ [ \ ] ^ _` sends the control character
  (Ctrl-A 01h ... Ctrl-_ 1Fh). Also: Ctrl-Space and Ctrl-2 NUL, Ctrl-6 RS
  (1Eh), Ctrl-- and Ctrl-/ US (1Fh), Ctrl-8 and Ctrl-? DEL (7Fh).
- **Alt** with a key sends ESC first (meta).
- **Tab** 09h, **Esc** 1Bh, **Space** 20h.
- **Backspace**: BS (08h), Ctrl-Backspace DEL (7Fh); the other way round
  with DECBKM reset (SET-UP's Backspace sends: DEL).
- **Return**: CR, or CR LF in New line mode (LNM).

## VT100 (ANSI mode)

| Key | Normal | Application mode |
|---|---|---|
| Up, Down, Right, Left | `ESC [ A`/`B`/`C`/`D` | `ESC O A`/`B`/`C`/`D` (DECCKM set) |
| Home, End | `ESC [ H`, `ESC [ F` | `ESC O H`, `ESC O F` |
| Insert, Delete | `ESC [ 2 ~`, `ESC [ 3 ~` | |
| Page Up, Page Down | `ESC [ 5 ~`, `ESC [ 6 ~` | |
| F1-F4 (PF1-PF4) | `ESC O P`, `Q`, `R`, `S` | |
| F5, F6, F7, F8 | `ESC [ 15 ~`, `17 ~`, `18 ~`, `19 ~` | |
| F9, F10, F11 | `ESC [ 20 ~`, `21 ~`, `23 ~` | |
| F12, Pause | open SET-UP | |

The keypad with **Num Lock on** follows the keypad mode:

| Key | Numeric mode | Application mode (DECKPAM) |
|---|---|---|
| 0-9 | `0`-`9` | `ESC O p` - `ESC O y` |
| `.` | `.` | `ESC O n` |
| `-` | `-` | `ESC O m` |
| `+` | `+` (the VT100's keypad comma) | `ESC O l` |
| `/`, `*`, `=` | `/`, `*`, `=` | `ESC O o`, `ESC O j`, `ESC O X` (xterm) |
| Enter | CR (CR LF in LNM) | `ESC O M` |

With **Num Lock off** the keypad is the cursor and editing keys, as on a
PC: 8 Up, 2 Down, 4 Left, 6 Right, 7 Home, 1 End, 9 Page Up, 3 Page Down,
0 Insert, `.` Delete.

## VT52 mode

| Key | Sends |
|---|---|
| Up, Down, Right, Left | `ESC A`, `ESC B`, `ESC C`, `ESC D` |
| F1-F4 | `ESC P`, `ESC Q`, `ESC R`, `ESC S` |
| keypad, alternate mode | `ESC ? x` (x as `ESC O x` above) |
| keypad, numeric mode | the characters |
| Home, End, Insert, Delete, Page Up/Down, F5-F11 | nothing (a VT52 has no such keys) |

## ADM-3A

| Key | Sends |
|---|---|
| Up, Down, Right, Left | ^K (0Bh), ^J (0Ah), ^L (0Ch), ^H (08h) |
| Home | ^^ (1Eh) |
| other editing and function keys | nothing |

## Special keys

| Key | Action |
|---|---|
| F12, Pause | SET-UP (nothing sent) |
| F11 in CP/M mode | the machine's reset (nothing sent) |
| any key while the screen is blanked | wakes it (not sent) |

Keys repeat after 0.5 s at about 30 a second.
