#ifndef KEYNAME_H
#define KEYNAME_H

// Readable text for the bytes a key sends, for the status line. Portable C,
// host-tested.
//
// Printable characters run together; controls get their names (ESC, CR, LF,
// BS, TAB, DEL, NUL, SP for a space) or caret form (^C), and bytes from 0x80
// are shown in hex (<9b>); each named byte is separated by a space. For
// example: "a", "ESC [A", "ESC OP", "ESC [24~", "CR LF", "^C", "ESC x".

#include <stddef.h>
#include <stdint.h>

// Write the description of data[0..len-1] into out (always terminated,
// truncated to fit).
void keyname(const uint8_t *data, size_t len, char *out, size_t size);

#endif
