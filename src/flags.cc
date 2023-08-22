#include "flags.hh"

constexpr const char *flags_chars= "pot[@$n0C<*%&";
static_assert(strlen(flags_chars) == C_ALL, "Keep in sync with Flags");
const char *flags_phrases[C_PLACED]= {"persistent", "optional", "trivial"};

unsigned flag_get_index(char c)
{
	switch (c) {
	case 'p':  return I_PERSISTENT;
	case 'o':  return I_OPTIONAL;
	case 't':  return I_TRIVIAL;
	case 'n':  return I_NEWLINE_SEPARATED;
	case '0':  return I_NUL_SEPARATED;
	case 'C':  return I_CODE;
	default:
		should_not_happen();
		return 0;
	}
}

bool render_flags(Flags flags, Parts &parts, Rendering rendering)
{
	TRACE_FUNCTION(SHOW, render_flags);
	if (!(rendering & R_SHOW_FLAGS))
		return false;
	string ret;
	for (unsigned i= 0; i < C_ALL; ++i) {
		unsigned test= flags & (1u << i);
		if (test) {
			ret += flags_chars[i];
		}
	}
	if (ret.empty())
		return false;
	ret= '-' + ret;
	parts.append_operator(ret);
	return true;
}

string show_flags(Flags flags, Style style)
{
	Parts parts;
	render_flags(flags, parts, R_SHOW_FLAGS);
	return show(parts, style);
}