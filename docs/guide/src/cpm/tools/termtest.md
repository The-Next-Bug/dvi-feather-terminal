# TERMTEST

```
B:TERMTEST
```

A check of the [terminal types](../../terminal/terminal-types.md), on the
`Z80TESTS` disk. Run it with `Z80TESTS` on B:, pick the type the terminal
is set to, then any key turns the page and Esc goes back to the menu. For
VT52, leave the terminal on VT100: TERMTEST switches it with `CSI ? 2 l`
and back with `ESC <`.

One page each for:

1. cursor addressing,
2. cursor moves (`OK` on every row when they are right),
3. erasing,
4. inserting and deleting lines,
5. scrolling,
6. wrapping at the last column,
7. line graphics,
8. the terminal's replies,
9. the codes the keys send (Ctrl-C ends that one).

Each page says what it should look like.

Install `Z80TESTS` again from the monitor (**I**) to get the latest
TERMTEST onto a card that already has the disk.
