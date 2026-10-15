#include "keyname.h"

#include <stdio.h>
#include <string.h>

static void append(char *out, size_t size, size_t *pos, const char *text) {
	size_t n = strlen(text);
	if (*pos + 1 >= size) {
		return;
	}
	if (n > size - 1 - *pos) {
		n = size - 1 - *pos;
	}
	memcpy(out + *pos, text, n);
	*pos += n;
	out[*pos] = 0;
}

static const char *control_name(uint8_t c) {
	switch (c) {
	case 0x00: return "NUL";
	case 0x08: return "BS";
	case 0x09: return "TAB";
	case 0x0a: return "LF";
	case 0x0d: return "CR";
	case 0x1b: return "ESC";
	case ' ': return "SP";
	case 0x7f: return "DEL";
	default: return NULL;
	}
}

void keyname(const uint8_t *data, size_t len, char *out, size_t size) {
	size_t pos = 0;
	if (size) {
		out[0] = 0;
	}
	// Whether the last thing written was a name (the next item needs a
	// space) or a printable character (more of them run on).
	enum { NOTHING, NAME, TEXT } last = NOTHING;
	for (size_t i = 0; i < len; ++i) {
		uint8_t c = data[i];
		char item[8];
		const char *name = control_name(c);
		if (name) {
			snprintf(item, sizeof item, "%s", name);
		} else if (c < 0x20) {
			snprintf(item, sizeof item, "^%c", c + '@');
		} else if (c >= 0x80) {
			snprintf(item, sizeof item, "<%02x>", c);
		} else {
			if (last == NAME) {
				append(out, size, &pos, " ");
			}
			item[0] = (char)c;
			item[1] = 0;
			append(out, size, &pos, item);
			last = TEXT;
			continue;
		}
		if (last != NOTHING) {
			append(out, size, &pos, " ");
		}
		append(out, size, &pos, item);
		last = NAME;
	}
}
