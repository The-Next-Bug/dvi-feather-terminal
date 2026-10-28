# ALTSCR

```
B:ALTSCR
```

A check of the alternate screen (xterm's `CSI ? 1049 h`, `?1047`, `?47`),
with the VT100 terminal type, on the `Z80TESTS` disk.

1. It fills the main screen with numbered rows in bold, underlined yellow
   and reverse, with an X in the middle.
2. It goes to the alternate screen with `?1049`, `?1047` and `?47` in turn,
   scrolls it, and comes back on a key: the main screen must be as it was.
   `?1049` also brings back the cursor and its colors, so `<1049` appears
   after the X in bold red.
3. The last step asks you to open and close SET-UP over the alternate
   screen, whose rows must come back.

Any key goes on; Ctrl-C leaves at once.
