/* radare - LGPL - Copyright 2009-2015 - pancake */

#include <r_reg.h>
#include <r_util.h>

static const char *parse_alias (RReg *reg, char **tok, const int n) {
	if (n != 2) return "Invalid syntax";
	int role = r_reg_get_name_idx (tok[0] + 1);
	return r_reg_set_name (reg, role, tok[1]) ?
		NULL : "Invalid alias";
}

// Sizes prepended with a dot are expressed in bits
// strtoul with base 0 allows the input to be in decimal/octal/hex format
#define parse_size(c) \
	((c)[0] == '.') ? \
		strtoul((c) + 1, &end, 10) : \
		strtoul((c), &end, 0) << 3;

static const char *parse_def (RReg *reg, char **tok, const int n) {
	RRegItem *item;
	char *end;
	int type;

	if (n != 5 && n != 6)
		return "Invalid syntax";

	type = r_reg_type_by_name (tok[0]);
	if (type < 0)
		return "Invalid register type";

	item = R_NEW0 (RRegItem);

	item->type = type;
	item->name = strdup (tok[1]);
	// All the numeric arguments are strictly checked
	item->size = parse_size (tok[2]);
	if (*end != '\0' || !item->size) {
		r_reg_item_free (item);
		return "Invalid size";
	}
	item->offset = parse_size (tok[3]);
	if (*end != '\0') {
		r_reg_item_free (item);
		return "Invalid offset";
	}
	item->packed_size = parse_size (tok[4]); 
	if (*end != '\0') {
		r_reg_item_free (item);
		return "Invalid packed size";
	}

	// Dynamically update the list of supported bit sizes
	reg->bits |= item->size;

	// This is optional
	if (n == 6)
		item->flags = strdup (tok[5]);

	// Don't allow duplicate registers
	if (r_reg_get (reg, item->name, R_REG_TYPE_ALL)) {
		r_reg_item_free (item);
		return "Duplicate register definition";
	}

	r_list_append (reg->regset[item->type].regs, item);

	// Update the overall profile size
	if (item->offset + item->size > reg->size)
		reg->size = item->offset + item->size;

	return NULL;
}

#define PARSER_MAX_TOKENS 8

R_API int r_reg_set_profile_string(RReg *reg, const char *str) {
	char *tok[PARSER_MAX_TOKENS];
	char tmp[128];
	int i, j, l;
	const char *p = str;

	if (!reg || !str)
		return false;

	// Same profile, no need to change
	if (reg->reg_profile_str && !strcmp (reg->reg_profile_str, str))
		return true;

	// Purge the old registers
	r_reg_free_internal (reg);

	// Cache the profile string
	reg->reg_profile_str = strdup (str);

	// Line number
	l = 0;
	// For every line
	do {
		// Increment line number
		l++;
		// Skip comment lines
		if (*p == '#') {
			while (*p != '\n')
				p++;
			continue;
		}
		j = 0;
		// For every word
		while (*p) {
			// Skip the whitespace
			while (*p == ' ' || *p == '\t')
				p++;
			// Skip the rest of the line is a comment is encountered
			if (*p == '#')
				while (*p != '\n')
					p++;
			// EOL ?
			if (*p == '\n')
				break;
			// Gather a handful of chars
			// Use isgraph instead of isprint because the latter considers ' ' printable
			for (i = 0; isgraph ((const unsigned char)*p) && i < sizeof(tmp) - 1;)
				tmp[i++] = *p++;
			tmp[i] = '\0';
			// Limit the number of tokens 
			if (j > PARSER_MAX_TOKENS - 1)
				break;
			// Save the token
			tok[j++] = strdup (tmp);
		}
		// Empty line, eww
		if (j) {
			// Do the actual parsing 
			char *first = tok[0];
			// Check whether it's defining an alias or a register
			const char *r = (*first == '=') ?
				parse_alias (reg, tok, j) :
				parse_def (reg, tok, j);
			// Clean up
			for (i = 0; i < j; i++)
				free(tok[i]);
			// Warn the user if something went wrong
			if (r) {
				eprintf("%s: Parse error @ line %d (%s)\n",
					__FUNCTION__, l, r);
				// Clean up
				r_reg_free_internal (reg);
				return false;
			}
		}
	} while (*p++);

	// Align to byte boundary if needed
	if (reg->size&7)
		reg->size += 8 - (reg->size&7);
	reg->size >>= 3; // bits to bytes (divide by 8)
	r_reg_fit_arena (reg);
	return true;
}

R_API int r_reg_set_profile(RReg *reg, const char *profile) {
	int ret;
	char *base, *file;
	char *str = r_file_slurp (profile, NULL);
	if (!str) {
		// XXX we must define this varname in r_lib.h /compiletime/
		base = r_sys_getenv ("LIBR_PLUGINS");
		if (base) {
			file = r_str_concat (base, profile);
			str = r_file_slurp (file, NULL);
			free (file);
		}
	}

	if (!str) {
		eprintf ("r_reg_set_profile: Cannot find '%s'\n", profile);
		return false;
	}
	
	ret = r_reg_set_profile_string (reg, str);
	free (str);
	return ret;
}
