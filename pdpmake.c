/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

/*
 * Check structures for make.
 */
/*
 * Include header for make
 */
#define _XOPEN_SOURCE 700
#if defined(__sun__)
# define __EXTENSIONS__
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define NORETURN __attribute__ ((__noreturn__))

// Resetting getopt(3) is hopelessly platform-dependent.  If command
// line options don't work as expected you may need to tweak this.
// The default should work for GNU libc and OpenBSD.
#if defined(__FreeBSD__) || defined(__NetBSD__)
# define GETOPT_RESET() do { \
	extern int optreset; \
	optind = 1; \
	optreset = 1; \
} while (0)
#elif defined(__sun__)
# define GETOPT_RESET() (optind = 1)
#else
# define GETOPT_RESET() (optind = 0)
#endif

// Supported POSIX levels
#define STD_POSIX_2017 0
#define STD_POSIX_2024 1

// If ENABLE_FEATURE_MAKE_EXTENSIONS is non-zero some non-POSIX extensions
// are enabled.
//
#ifndef ENABLE_FEATURE_MAKE_EXTENSIONS
# define ENABLE_FEATURE_MAKE_EXTENSIONS 1
#endif

#ifndef DEFAULT_POSIX_LEVEL
# define DEFAULT_POSIX_LEVEL STD_POSIX_2024
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
# define IF_FEATURE_MAKE_EXTENSIONS(...) __VA_ARGS__
# define IF_NOT_FEATURE_MAKE_EXTENSIONS(...)
# define POSIX_2017 (posix && posix_level == STD_POSIX_2017)
#else
# define IF_FEATURE_MAKE_EXTENSIONS(...)
# define IF_NOT_FEATURE_MAKE_EXTENSIONS(...) __VA_ARGS__
#endif

// IF ENABLE_FEATURE_MAKE_POSIX_2024 is non-zero POSIX 2024 features
// are enabled.
#ifndef ENABLE_FEATURE_MAKE_POSIX_2024
# define ENABLE_FEATURE_MAKE_POSIX_2024 ENABLE_FEATURE_MAKE_EXTENSIONS
#endif

#if ENABLE_FEATURE_MAKE_POSIX_2024
# define IF_FEATURE_MAKE_POSIX_2024(...) __VA_ARGS__
# define IF_NOT_FEATURE_MAKE_POSIX_2024(...)
#else
# define IF_FEATURE_MAKE_POSIX_2024(...)
# define IF_NOT_FEATURE_MAKE_POSIX_2024(...) __VA_ARGS__
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
# define POSIX_2017 (posix && posix_level == STD_POSIX_2017)
#elif ENABLE_FEATURE_MAKE_POSIX_2024
# define POSIX_2017 FALSE
#endif

// If ENABLE_FEATURE_CLEAN_UP is non-zero all allocated structures are
// freed at the end of main().  This isn't necessary but it's a nice test.
#ifndef ENABLE_FEATURE_CLEAN_UP
# define ENABLE_FEATURE_CLEAN_UP 0
#endif

#define TRUE		(1)
#define FALSE		(0)
#define MAX(a,b)	((a)>(b)?(a):(b))

#if defined(__GLIBC__) && ENABLE_FEATURE_MAKE_EXTENSIONS
// By default GNU libc getopt(3) allows options and non-options to be
// mixed.  Turn this off in POSIX mode.  The '+' prefix in OPTSTR1 is
// otherwise unused and should be skipped.
# define OPT_OFFSET + !posix
#else
# define OPT_OFFSET
#endif

#ifdef __clang__
#pragma clang diagnostic ignored "-Wstring-plus-int"
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define OPTSTR1 "+ehij:knqrsSt"
#elif ENABLE_FEATURE_MAKE_POSIX_2024
#define OPTSTR1 "+eij:knqrsSt"
#else
#define OPTSTR1 "+eiknqrsSt"
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define OPTSTR2 "pf:C:x:"
#else
#define OPTSTR2 "pf:"
#endif

enum {
	OPTBIT_e = 0,
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_h,)
	OPTBIT_i,
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_j,)
	OPTBIT_k,
	OPTBIT_n,
	OPTBIT_q,
	OPTBIT_r,
	OPTBIT_s,
	OPTBIT_S,
	OPTBIT_t,
	OPTBIT_p,
	OPTBIT_f,
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_C,)
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_x,)
	OPTBIT_precious,
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_phony,)
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_include,)
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_make,)

	OPT_e = (1 << OPTBIT_e),
	OPT_h = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_h)) + 0,
	OPT_i = (1 << OPTBIT_i),
	OPT_j = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_j)) + 0,
	OPT_k = (1 << OPTBIT_k),
	OPT_n = (1 << OPTBIT_n),
	OPT_q = (1 << OPTBIT_q),
	OPT_r = (1 << OPTBIT_r),
	OPT_s = (1 << OPTBIT_s),
	OPT_S = (1 << OPTBIT_S),
	OPT_t = (1 << OPTBIT_t),
	// These options aren't allowed in MAKEFLAGS
	OPT_p = (1 << OPTBIT_p),
	OPT_f = (1 << OPTBIT_f),
	OPT_C = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_C)) + 0,
	OPT_x = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_x)) + 0,
	// The following aren't command line options and must be last
	OPT_precious = (1 << OPTBIT_precious),
	OPT_phony = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_phony)) + 0,
	OPT_include = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_include)) + 0,
	OPT_make = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_make)) + 0,
};

// Options in OPTSTR1 that aren't included in MAKEFLAGS
#define OPT_MASK  (~OPT_S)

#define useenv    (opts & OPT_e)
#define ignore    (opts & OPT_i)
#define errcont   (opts & OPT_k)
#define dryrun    (opts & OPT_n)
#define print     (opts & OPT_p)
#define quest     (opts & OPT_q)
#define norules   (opts & OPT_r)
#define silent    (opts & OPT_s)
#define dotouch   (opts & OPT_t)
#define precious  (opts & OPT_precious)
#define doinclude (opts & OPT_include)
#define domake    (opts & OPT_make)

// A name.  This represents a file, either to be made, or pre-existing.
struct name {
	struct name *n_next;	// Next in the list of names
	char *n_name;			// Called
	struct rule *n_rule;	// Rules to build this (prerequisites/commands)
	struct timespec n_tim;	// Modification time of this name
	uint16_t n_flag;		// Info about the name
};

#define N_DOING		0x01	// Name in process of being built
#define N_DONE		0x02	// Name looked at
#define N_TARGET	0x04	// Name is a target
#define N_PRECIOUS	0x08	// Target is precious
#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define N_DOUBLE	0x10	// Double-colon target
#else
#define N_DOUBLE	0x00	// No support for double-colon target
#endif
#define N_SILENT	0x20	// Build target silently
#define N_IGNORE	0x40	// Ignore build errors
#define N_SPECIAL	0x80	// Special target
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
#define N_MARK		0x100	// Mark for deduplication
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
#define N_PHONY		0x200	// Name is a phony target
#else
#define N_PHONY		0		// No support for phony targets
#endif
#define N_INFERENCE	0x400	// Inference rule

// List of rules to build a target
struct rule {
	struct rule *r_next;	// Next rule
	struct depend *r_dep;	// Prerequisites for this rule
	struct cmd *r_cmd;		// Commands for this rule
};

// NOTE: the layout of the following two structures must be compatible.

// List of prerequisites for a rule
struct depend {
	struct depend *d_next;	// Next prerequisite
	struct name *d_name;	// Name of prerequisite
	int d_refcnt;			// Reference count
};

// List of commands for a rule
struct cmd {
	struct cmd *c_next;		// Next command line
	char *c_cmd;			// Text of command line
	int c_refcnt;			// Reference count
	const char *c_makefile;	// Makefile in which command was defined
	int c_dispno;			// Line number within makefile
};

// Macro storage
struct macro {
	struct macro *m_next;	// Next variable
	char *m_name;			// Its name
	char *m_val;			// Its value
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
	bool m_immediate;		// Immediate-expansion macro set using ::=
#endif
	bool m_flag;			// Infinite loop check
	uint8_t m_level;		// Level at which macro was created
};

// List of file names
struct file {
	struct file *f_next;
	char *f_name;
};

// Flags passed to setmacro()
#define M_IMMEDIATE  0x08	// immediate-expansion macro is being defined
#define M_VALID      0x10	// assert macro name is valid
#define M_ENVIRON    0x20	// macro imported from environment

#define HTABSIZE 199

// Constants for PRAGMA.  Order must match strings in set_pragma().
enum {
	BIT_MACRO_NAME = 0,
	BIT_TARGET_NAME,
	BIT_COMMAND_COMMENT,
	BIT_EMPTY_SUFFIX,
#if defined(__CYGWIN__)
	BIT_WINDOWS,
#endif
	BIT_POSIX_2017,
	BIT_POSIX_2024,
	BIT_POSIX_202X,

	P_MACRO_NAME = (1 << BIT_MACRO_NAME),
	P_TARGET_NAME = (1 << BIT_TARGET_NAME),
	P_COMMAND_COMMENT = (1 << BIT_COMMAND_COMMENT),
	P_EMPTY_SUFFIX = (1 << BIT_EMPTY_SUFFIX),
#if defined(__CYGWIN__)
	P_WINDOWS = (1 << BIT_WINDOWS),
#endif
};

// Status of make()
#define MAKE_FAILURE		0x01
#define MAKE_DIDSOMETHING	0x02

extern const char *myname;
extern const char *makefile;
extern struct name *namehead[HTABSIZE];
extern struct macro *macrohead[HTABSIZE];
extern struct name *firstname;
extern struct name *target;
extern uint32_t opts;
extern int lineno;
extern int dispno;
#if ENABLE_FEATURE_MAKE_POSIX_2024
extern char *numjobs;
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
extern bool posix;
extern bool seen_first;
extern unsigned char pragma;
extern unsigned char posix_level;
#endif

// Return TRUE if c is allowed in a POSIX 2017 macro or target name
#define ispname(c) (isalpha(c) || isdigit(c) || c == '.' || c == '_')
// Return TRUE if c is in the POSIX 'portable filename character set'
#define isfname(c) (ispname(c) || c == '-')

void print_details(void);
#if !ENABLE_FEATURE_MAKE_POSIX_2024
#define expand_macros(s, e) expand_macros(s)
#endif
char *expand_macros(const char *str, int except_dollar);
void input(FILE *fd, int ilevel);
struct macro *getmp(const char *name);
void setmacro(const char *name, const char *val, int level);
void freemacros(void);
void remove_target(void);
int make(struct name *np, int level);
char *splitlib(const char *name, char **member);
void modtime(struct name *np);
char *suffix(const char *name);
int is_suffix(const char *s);
struct name *dyndep(struct name *np, struct rule *imprule);
char *getrules(char *s, int size);
struct name *findname(const char *name);
struct name *newname(const char *name);
struct cmd *getcmd(struct name *np);
void freenames(void);
struct depend *newdep(struct name *np, struct depend *dp);
void freedeps(struct depend *dp);
struct cmd *newcmd(char *str, struct cmd *cp);
void freecmds(struct cmd *cp);
void freerules(struct rule *rp);
void set_pragma(const char *name);
void addrule(struct name *np, struct depend *dp, struct cmd *cp, int flag);
void diagnostic(const char *msg, ...);
void error(const char *msg, ...) NORETURN;
void error_unexpected(const char *s) NORETURN;
void error_in_inference_rule(const char *s) NORETURN;
void error_not_allowed(const char *s, const char *t);
void warning(const char *msg, ...);
void *xmalloc(size_t len);
void *xrealloc(void *ptr, size_t len);
char *xconcat3(const char *s1, const char *s2, const char *s3);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xappendword(const char *str, const char *word);
unsigned int getbucket(const char *name);
struct file *newfile(char *str, struct file *fphead);
void freefiles(struct file *fp);
int is_valid_target(const char *name);
void pragmas_from_env(void);
void pragmas_to_env(void);

static void
print_name(struct name *np)
{
	if (np == firstname)
		printf("# default target\n");
	printf("%s:", np->n_name);
	if ((np->n_flag & N_DOUBLE))
		putchar(':');
}

static void
print_prerequisites(struct rule *rp)
{
	struct depend *dp;

	for (dp = rp->r_dep; dp; dp = dp->d_next)
		printf(" %s", dp->d_name->n_name);
}

static void
print_commands(struct rule *rp)
{
	struct cmd *cp;

	for (cp = rp->r_cmd; cp; cp = cp->c_next)
		printf("\t%s\n", cp->c_cmd);
}

void
print_details(void)
{
	int i;
	struct macro *mp;
	struct name *np;
	struct rule *rp;

	for (i = 0; i < HTABSIZE; i++)
		for (mp = macrohead[i]; mp; mp = mp->m_next)
			printf("%s = %s\n", mp->m_name, mp->m_val);
	putchar('\n');

	for (i = 0; i < HTABSIZE; i++) {
		for (np = namehead[i]; np; np = np->n_next) {
			if (!(np->n_flag & N_DOUBLE)) {
				print_name(np);
				for (rp = np->n_rule; rp; rp = rp->r_next) {
					print_prerequisites(rp);
				}
				putchar('\n');

				for (rp = np->n_rule; rp; rp = rp->r_next) {
					print_commands(rp);
				}
				putchar('\n');
			} else {
				for (rp = np->n_rule; rp; rp = rp->r_next) {
					print_name(np);
					print_prerequisites(rp);
					putchar('\n');

					print_commands(rp);
					putchar('\n');
				}
			}
		}
	}
}
/*
 * Parse a makefile
 */
#include <glob.h>

int lineno;	// Physical line number in file
int dispno;	// Line number for display purposes

/*
 * Return a pointer to the next blank-delimited word or NULL if
 * there are none left.
 */
static char *
gettok(char **ptr)
{
	char *p;

	while (isblank(**ptr))	// Skip blanks
		(*ptr)++;

	if (**ptr == '\0')	// Nothing after blanks
		return NULL;

	p = *ptr;		// Word starts here

	while (**ptr != '\0' && !isblank(**ptr))
		(*ptr)++;	// Find end of word

	// Terminate token and move on unless already at end of string
	if (**ptr != '\0')
		*(*ptr)++ = '\0';

	return(p);
}

/*
 * Skip over (possibly adjacent or nested) macro expansions.
 */
static char *
skip_macro(const char *s)
{
	while (*s && s[0] == '$') {
		if (s[1] == '(' || s[1] == '{') {
			char end = *++s == '(' ? ')' : '}';
			while (*s && *s != end)
				s = skip_macro(s + 1);
			if (*s == end)
				++s;
		} else if (s[1] != '\0') {
			s += 2;
		} else {
			break;
		}
	}
	return (char *)s;
}

#if !ENABLE_FEATURE_MAKE_POSIX_2024
# define modify_words(v, m, lf, lr, fp, rp, fs, rs) \
			modify_words(v, m, lf, lr, fs, rs)
#endif
/*
 * Process each whitespace-separated word in the input string:
 *
 * - replace paths with their directory or filename part
 * - replace prefixes and suffixes
 *
 * Returns an allocated string or NULL if the input is unmodified.
 */
static char *
modify_words(const char *val, int modifier, size_t lenf, size_t lenr,
				const char *find_pref, const char *repl_pref,
				const char *find_suff, const char *repl_suff)
{
	char *s, *copy, *word, *sep, *newword, *buf = NULL;
#if ENABLE_FEATURE_MAKE_POSIX_2024
	size_t find_pref_len = 0, find_suff_len = 0;
#endif

	if (!modifier && lenf == 0 && lenr == 0)
		return buf;

#if ENABLE_FEATURE_MAKE_POSIX_2024
	if (find_pref) {
		// get length of find prefix, e.g: src/
		find_pref_len = strlen(find_pref);
		// get length of find suffix, e.g: .c
		find_suff_len = lenf - find_pref_len - 1;
	}
#endif

	s = copy = xstrdup(val);
	while ((word = gettok(&s)) != NULL) {
		newword = NULL;
		if (modifier) {
			sep = strrchr(word, '/');
			if (modifier == 'D') {
				if (!sep) {
					word[0] = '.';	// no '/', return "."
					sep = word + 1;
				} else if (sep == word) {
					// '/' at start of word, return "/"
					sep = word + 1;
				}
				// else terminate at separator
				*sep = '\0';
			} else if (/* modifier == 'F' && */ sep) {
				word = sep + 1;
			}
		}
		if (IF_FEATURE_MAKE_POSIX_2024(find_pref != NULL ||)
				lenf != 0 || lenr != 0) {
			size_t lenw = strlen(word);
#if ENABLE_FEATURE_MAKE_POSIX_2024
			// This code implements pattern macro expansions:
			//    https://austingroupbugs.net/view.php?id=519
			//
			// find: <prefix>%<suffix>
			// example: src/%.c
			//
			// For a pattern of the form:
			//    $(string1:[op]%[os]=[np][%][ns])
			// lenf is the length of [op]%[os].  So lenf >= 1.
			if (find_pref != NULL && lenw + 1 >= lenf) {
				// If prefix and suffix of word match find_pref and
				// find_suff, then do substitution.
				if (strncmp(word, find_pref, find_pref_len) == 0 &&
						strcmp(word + lenw - find_suff_len, find_suff) == 0) {
					// replace: <prefix>[%<suffix>]
					// example: build/%.o or build/all.o (notice no %)
					// If repl_suff is NULL, replace whole word with repl_pref.
					if (!repl_suff) {
						word = newword = xstrdup(repl_pref);
					} else {
						word[lenw - find_suff_len] = '\0';
						word = newword = xconcat3(repl_pref,
									word + find_pref_len, repl_suff);
					}
				}
			} else
#endif
			if (lenw >= lenf && strcmp(word + lenw - lenf, find_suff) == 0) {
				word[lenw - lenf] = '\0';
				word = newword = xconcat3(word, repl_suff, "");
			}
		}
		buf = xappendword(buf, word);
		free(newword);
	}
	free(copy);
	return buf;
}

/*
 * Return a pointer to the next instance of a given character.  Macro
 * expansions are skipped so the ':' and '=' in $(VAR:.s1=.s2) aren't
 * detected as separators in macro definitions.  Some other situations
 * also require skipping the internals of a macro expansion.
 */
static char *
find_char(const char *str, int c)
{
	const char *s;

	for (s = skip_macro(str); *s; s = skip_macro(s + 1)) {
		if (*s == c)
			return (char *)s;
	}
	return NULL;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS && defined(__CYGWIN__)
/*
 * Check for a target rule by searching for a colon that isn't
 * part of a Windows path.  Return a pointer to the colon or NULL.
 */
static char *
find_colon(char *p)
{
	char *q;

	for (q = p; (q = strchr(q, ':')); ++q) {
		if (posix && !(pragma & P_WINDOWS))
			break;
		if (q == p || !isalpha(q[-1]) || q[1] != '/')
			break;
	}
	return q;
}
#else
# define find_colon(s) strchr(s, ':')
#endif

/*
 * Recursively expand any macros in str to an allocated string.
 */
char *
expand_macros(const char *str, int except_dollar)
{
	char *exp, *newexp, *s, *t, *p, *q, *name;
	char *find, *replace, *modified;
	char *expval, *expfind, *find_suff, *repl_suff;
#if ENABLE_FEATURE_MAKE_POSIX_2024
	char *find_pref = NULL, *repl_pref = NULL;
#endif
	size_t lenf, lenr;
	char modifier;
	struct macro *mp;

	exp = xstrdup(str);
	for (t = exp; *t; t++) {
		if (*t == '$') {
			if (t[1] == '\0') {
				break;
			}
#if ENABLE_FEATURE_MAKE_POSIX_2024
			if (t[1] == '$' && except_dollar) {
				t++;
				continue;
			}
#endif
			// Need to expand a macro.  Find its extent (s to t inclusive)
			// and take a copy of its content.
			s = t;
			t++;
			if (*t == '{' || *t == '(') {
				t = find_char(t, *t == '{' ? '}' : ')');
				if (t == NULL)
					error("unterminated variable '%s'", s);
				name = xstrndup(s + 2, t - s - 2);
			} else {
				name = xmalloc(2);
				name[0] = *t;
				name[1] = '\0';
			}

			// Only do suffix replacement or pattern macro expansion
			// if both ':' and '=' are found, plus a '%' for the latter.
			// Suffix replacement is indicated by
			// find_pref == NULL && (lenf != 0 || lenr != 0);
			// pattern macro expansion by find_pref != NULL.
			expfind = NULL;
			find_suff = repl_suff = NULL;
			lenf = lenr = 0;
			if ((find = find_char(name, ':'))) {
				*find++ = '\0';
				expfind = expand_macros(find, FALSE);
				if ((replace = find_char(expfind, '='))) {
					*replace++ = '\0';
					lenf = strlen(expfind);
#if ENABLE_FEATURE_MAKE_POSIX_2024
					if (!POSIX_2017 && (find_suff = strchr(expfind, '%'))) {
						find_pref = expfind;
						repl_pref = replace;
						*find_suff++ = '\0';
						if ((repl_suff = strchr(replace, '%')))
							*repl_suff++ = '\0';
					} else
#endif
					{
						if (IF_FEATURE_MAKE_EXTENSIONS(posix &&
									!(pragma & P_EMPTY_SUFFIX) &&)
								lenf == 0)
							error("empty suffix%s",
								!ENABLE_FEATURE_MAKE_EXTENSIONS ? "" :
									": allow with pragma empty_suffix");
						find_suff = expfind;
						repl_suff = replace;
						lenr = strlen(repl_suff);
					}
				}
			}

			p = q = name;
#if ENABLE_FEATURE_MAKE_POSIX_2024
			// If not in POSIX mode expand macros in the name.
			if (!POSIX_2017) {
				char *expname = expand_macros(name, FALSE);
				free(name);
				name = expname;
			} else
#endif
			// Skip over nested expansions in name
			do {
				*q++ = *p;
			} while ((p = skip_macro(p + 1)) && *p);

			// The internal macros support 'D' and 'F' modifiers
			modifier = '\0';
			switch (name[0]) {
#if ENABLE_FEATURE_MAKE_POSIX_2024
			case '^':
			case '+':
				if (POSIX_2017)
					break;
				// fall through
#endif
			case '@': case '%': case '?': case '<': case '*':
				if ((name[1] == 'D' || name[1] == 'F') && name[2] == '\0') {
					modifier = name[1];
					name[1] = '\0';
				}
				break;
			}

			modified = NULL;
			if ((mp = getmp(name)))  {
				// Recursive expansion
				if (mp->m_flag)
					error("recursive macro %s", name);
#if ENABLE_FEATURE_MAKE_POSIX_2024
				// Note if we've expanded $(MAKE)
				if (strcmp(name, "MAKE") == 0)
					opts |= OPT_make;
#endif
				mp->m_flag = TRUE;
				expval = expand_macros(mp->m_val, FALSE);
				mp->m_flag = FALSE;
				modified = modify_words(expval, modifier, lenf, lenr,
								find_pref, repl_pref, find_suff, repl_suff);
				if (modified)
					free(expval);
				else
					modified = expval;
			}
			free(name);
			free(expfind);

			if (modified && *modified) {
				// The text to be replaced by the macro expansion is
				// from s to t inclusive.
				*s = '\0';
				newexp = xconcat3(exp, modified, t + 1);
				t = newexp + (s - exp) + strlen(modified) - 1;
				free(exp);
				exp = newexp;
			} else {
				// Macro wasn't expanded or expanded to nothing.
				// Close the space occupied by the macro reference.
				q = t + 1;
				t = s - 1;
				while ((*s++ = *q++))
					continue;
			}
			free(modified);
		}
	}
	return exp;
}

/*
 * Process a non-command line
 */
static void
process_line(char *s)
{
	char *t;

	// Strip comment
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// don't treat '#' in macro expansion as a comment
	// nor '#' outside macro expansion preceded by backslash
	if (!posix) {
		char *u = s;
		while ((t = find_char(u, '#')) && t > u && t[-1] == '\\') {
			for (u = t; *u; ++u) {
				u[-1] = u[0];
			}
			*u = '\0';
			u = t;
		}
	} else
#endif
		t = strchr(s, '#');
	if (t)
		*t = '\0';

	// Replace escaped newline and any leading white space on the
	// following line with a single space.  Stop processing at a
	// non-escaped newline.
	for (t = s; *s && *s != '\n'; ) {
		if (s[0] == '\\' && s[1] == '\n') {
			s += 2;
			while (isspace(*s))
				++s;
			*t++ = ' ';
		} else {
			*t++ = *s++;
		}
	}
	*t = '\0';
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
enum {
	INITIAL = 0,
	SKIP_LINE = 1 << 0,
	EXPECT_ELSE = 1 << 1,
	GOT_MATCH = 1 << 2
};

#define IF_MAX 10

static uint8_t clevel = 0;
static uint8_t cstate[IF_MAX + 1] = {INITIAL};

/*
 * Extract strings following ifeq/ifneq and compare them.
 * Return -1 on error.
 */
static int
compare_strings(char *arg1)
{
	char *arg2, *end, term, *t1, *t2;
	int ret;

	// Get first string terminator.
	if (arg1[0] == '(')
		term = ',';
	else if (arg1[0] == '"' || arg1[0] == '\'')
		term =  arg1[0];
	else
		return -1;

	arg2 = find_char(++arg1, term);
	if (arg2 == NULL)
		return -1;
	*arg2++ = '\0';

	// Get second string terminator.
	if (term == ',') {
		term = ')';
	} else {
		// Skip spaces between quoted strings.
		while (isspace(arg2[0]))
			arg2++;
		if (arg2[0] == '"' || arg2[0] == '\'')
			term = arg2[0];
		else
			return -1;
		++arg2;
	}

	end = find_char(arg2, term);
	if (end == NULL)
		return -1;
	*end++ = '\0';

	if (gettok(&end) != NULL) {
		warning("unexpected text");
	}

	t1 = expand_macros(arg1, FALSE);
	t2 = expand_macros(arg2, FALSE);

	ret = strcmp(t1, t2) == 0;
	free(t1);
	free(t2);
	return ret;
}

/*
 * Process conditional directives and return TRUE if the current line
 * should be skipped.
 */
static int
skip_line(const char *str1)
{
	char *copy, *q, *token;
	bool new_level = TRUE;
	// Default is to return skip flag for current level
	int ret = cstate[clevel] & SKIP_LINE;

	q = copy = xstrdup(str1);
	process_line(copy);
	if ((token = gettok(&q)) != NULL) {
		if (strcmp(token, "endif") == 0) {
			if (gettok(&q) != NULL)
				error_unexpected("text");
			if (clevel == 0)
				error_unexpected(token);
			--clevel;
			ret = TRUE;
			goto end;
		} else if (strcmp(token, "else") == 0) {
			if (!(cstate[clevel] & EXPECT_ELSE))
				error_unexpected(token);

			// If an earlier condition matched we'll now skip lines.
			// If not we don't, though an 'else if' may override this.
			if ((cstate[clevel] & GOT_MATCH))
				cstate[clevel] |= SKIP_LINE;
			else
				cstate[clevel] &= ~SKIP_LINE;

			token = gettok(&q);
			if (token == NULL) {
				// Simple else with no conditional directive
				cstate[clevel] &= ~EXPECT_ELSE;
				ret = TRUE;
				goto end;
			} else {
				// A conditional directive is now required ('else if').
				new_level = FALSE;
			}
		}

		if (strcmp(token, "ifdef") == 0 || strcmp(token, "ifndef") == 0 ||
				strcmp(token, "ifeq") == 0 || strcmp(token, "ifneq") == 0) {
			int match;

			if (token[2] == 'd' || token[3] == 'd') {
				// ifdef/ifndef: find out if macro is defined.
				char *name = gettok(&q);
				if (name != NULL && gettok(&q) == NULL) {
					char *t = expand_macros(name, FALSE);
					struct macro *mp = getmp(t);
					match = mp != NULL && mp->m_val[0] != '\0';
					free(t);
				} else {
					match = -1;
				}
			} else {
				// ifeq/ifneq: compare strings.
				match = compare_strings(q);
			}

			if (match >= 0) {
				if (new_level) {
					// Start a new level.
					if (clevel == IF_MAX)
						error("nesting too deep");
					++clevel;
					cstate[clevel] = EXPECT_ELSE | SKIP_LINE;
					// If we were skipping lines at the previous level
					// we need to continue doing that unconditionally
					// at the new level.
					if ((cstate[clevel - 1] & SKIP_LINE))
						cstate[clevel] |= GOT_MATCH;
				}

				if (!(cstate[clevel] & GOT_MATCH)) {
					if (token[2] == 'n')
						match = !match;
					if (match) {
						cstate[clevel] &= ~SKIP_LINE;
						cstate[clevel] |= GOT_MATCH;
					}
				}
			} else {
				error("invalid condition");
			}
			ret = TRUE;
		} else if (!new_level) {
			error("missing conditional");
		}
	}
 end:
	free(copy);
	return ret;
}
#endif

/*
 * If fd is NULL read the built-in rules.  Otherwise read from the
 * specified file descriptor.
 */
static char *
make_fgets(char *s, int size, FILE *fd)
{
	return fd ? fgets(s, size, fd) : getrules(s, size);
}

/*
 * Read a newline-terminated line into an allocated string.
 * Backslash-escaped newlines don't terminate the line.
 * Ignore comment lines.  Return NULL on EOF.
 */
static char *
readline(FILE *fd, int want_command)
{
	char *p, *str = NULL;
	int pos = 0;
	int len = 0;

	for (;;) {
		// We need room for at least one character and a NUL terminator
		if (len - pos > 1 &&
				make_fgets(str + pos, len - pos, fd) == NULL) {
			if (pos)
				return str;
			free(str);
			return NULL;	// EOF
		}

		if (len - pos < 2 || (p = strchr(str + pos, '\n')) == NULL) {
			// Need more room
			if (len)
				pos = len - 1;
			len += 256;
			str = xrealloc(str, len);
			continue;
		}
		lineno++;

		// Remove CR before LF
		if (p != str && p[-1] == '\r') {
			p[-1] = '\n';
			*p-- = '\0';
		}

		// Keep going if newline has been escaped
		if (p != str && p[-1] == '\\') {
			pos = p - str + 1;
			continue;
		}
		dispno = lineno;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// Check for lines that are conditionally skipped.
		if (posix || !skip_line(str))
#endif
		{
			if (want_command && *str == '\t')
				return str;

			// Check for comment lines
			p = str;
			while (isblank(*p))
				p++;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
			if (*p != '\n' && (posix ? *str != '#' : *p != '#'))
#else
			if (*p != '\n' && *str != '#')
#endif
				return str;
		}

		pos = 0;
	}
}

/*
 * Return TRUE if the argument is a known suffix.
 */
int
is_suffix(const char *s)
{
	struct name *np;
	struct rule *rp;
	struct depend *dp;

	np = newname(".SUFFIXES");
	for (rp = np->n_rule; rp; rp = rp->r_next) {
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			if (strcmp(s, dp->d_name->n_name) == 0) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

enum {
	T_NORMAL    =  0,
	T_SPECIAL   = (1 << 0),
	T_INFERENCE = (1 << 1), // Inference rule
	T_NOPREREQ  = (1 << 2), // If set must not have prerequisites
	T_COMMAND   = (1 << 3), // If set must have commands, if unset must not
};

/*
 * Determine if the argument is a special target and return a set
 * of flags indicating its properties.
 */
static int
target_type(char *s)
{
	char *sfx;
	int ret;
	static const char *s_name[] = {
		".DEFAULT",
		".POSIX",
		".IGNORE",
		".PRECIOUS",
		".SILENT",
		".SUFFIXES",
#if ENABLE_FEATURE_MAKE_POSIX_2024
		".PHONY",
		".NOTPARALLEL",
		".WAIT",
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		".PRAGMA",
#endif
	};

	static const uint8_t s_type[] = {
		T_SPECIAL | T_NOPREREQ | T_COMMAND,
		T_SPECIAL | T_NOPREREQ,
		T_SPECIAL,
		T_SPECIAL,
		T_SPECIAL,
		T_SPECIAL,
#if ENABLE_FEATURE_MAKE_POSIX_2024
		T_SPECIAL,
		T_SPECIAL | T_NOPREREQ,
		T_SPECIAL | T_NOPREREQ,
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		T_SPECIAL,
#endif
	};

	if (*s != '.')
		return T_NORMAL;

	// Check for one of the known special targets
	for (ret = 0; ret < sizeof(s_name)/sizeof(s_name[0]); ret++)
		if (strcmp(s_name[ret], s) == 0)
			return s_type[ret];

	// Check for an inference rule
	ret = T_NORMAL;
	sfx = suffix(s);
	if (is_suffix(sfx)) {
		if (s == sfx) {	// Single suffix rule
			ret = T_INFERENCE | T_NOPREREQ | T_COMMAND;
		} else {
			// Suffix is valid, check that prefix is too
			*sfx = '\0';
			if (is_suffix(s))
				ret = T_INFERENCE | T_NOPREREQ | T_COMMAND;
			*sfx = '.';
		}
	}
	return ret;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
static int
ends_with_bracket(const char *s)
{
	const char *t = strrchr(s, ')');
	return t && t[1] == '\0';
}
#endif

/*
 * Process a command line
 */
static char *
process_command(char *s)
{
	char *t, *u;
#if ENABLE_FEATURE_MAKE_POSIX_2024
	int len;
	char *outside;
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if (!(pragma & P_COMMAND_COMMENT) && posix) {
		// POSIX strips comments from command lines
		t = strchr(s, '#');
		if (t) {
			*t = '\0';
			warning("comment in command removed: keep with pragma command_comment");
		}
	}
#endif

#if ENABLE_FEATURE_MAKE_POSIX_2024
	len = strlen(s) + 1;
	outside = xmalloc(len);
	memset(outside, 0, len);
	for (t = skip_macro(s); *t; t = skip_macro(t + 1)) {
		outside[t - s] = 1;
	}
#endif

	// Process escaped newlines.  Stop at first non-escaped newline.
	for (t = u = s; *u && *u != '\n'; ) {
		if (u[0] == '\\' && u[1] == '\n') {
#if ENABLE_FEATURE_MAKE_POSIX_2024
			if (POSIX_2017 || outside[u - s]) {
#endif
				// Outside macro: remove tab following escaped newline.
				*t++ = *u++;
				*t++ = *u++;
				u += (*u == '\t');
#if ENABLE_FEATURE_MAKE_POSIX_2024
			} else {
				// Inside macro: replace escaped newline and any leading
				// whitespace on the following line with a single space.
				u += 2;
				while (isspace(*u))
					++u;
				*t++ = ' ';
			}
#endif
		} else {
			*t++ = *u++;
		}
	}
	*t = '\0';
#if ENABLE_FEATURE_MAKE_POSIX_2024
	free(outside);
#endif
	return s;
}

#if ENABLE_FEATURE_MAKE_POSIX_2024
static char *
run_command(const char *cmd)
{
	FILE *fd;
	char *s, *val = NULL;
	char buf[256];
	size_t len = 0, nread;

	if ((fd = popen(cmd, "r")) == NULL)
		return val;

	for (;;) {
		nread = fread(buf, 1, sizeof(buf), fd);
		if (nread == 0)
			break;

		val = xrealloc(val, len + nread + 1);
		memcpy(val + len, buf, nread);
		len += nread;
		val[len] = '\0';
	}
	pclose(fd);

	if (val == NULL)
		return val;

	// Strip leading whitespace in POSIX 2024 mode
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if (posix)
#endif
	{
		s = val;
		while (isspace(*s)) {
			++s;
			--len;
		}

		if (len == 0) {
			free(val);
			return NULL;
		}
		memmove(val, s, len + 1);
	}

	// Remove one newline from the end (BSD compatibility)
	if (val[len - 1] == '\n')
		val[len - 1] = '\0';
	// Other newlines are changed to spaces
	for (s = val; *s; ++s) {
		if (*s == '\n')
			*s = ' ';
	}
	return val;
}
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
/*
 * Check for an unescaped wildcard character
 */
static int wildchar(const char *p)
{
	while (*p) {
		switch (*p) {
		case '?':
		case '*':
		case '[':
			return 1;
		case '\\':
			if (p[1] != '\0')
				++p;
			break;
		}
		++p;
	}
	return 0;
}

/*
 * Expand any wildcards in a pattern.  Return TRUE if a match is
 * found, in which case the caller should call globfree() on the
 * glob_t structure.
 */
static int
wildcard(char *p, glob_t *gd)
{
	int ret;
	char *s;

	// Don't call glob() if there are no wildcards.
	if (!wildchar(p)) {
 nomatch:
		// Remove backslashes from the name.
		for (s = p; *p; ++p) {
			if (*p == '\\' && p[1] != '\0')
				continue;
			*s++ = *p;
		}
		*s = '\0';
		return 0;
	}

	memset(gd, 0, sizeof(*gd));
	ret = glob(p, GLOB_NOSORT, NULL, gd);
	if (ret == GLOB_NOMATCH) {
		globfree(gd);
		goto nomatch;
	} else if (ret != 0) {
		error("glob error for '%s'", p);
	}
	return 1;
}

void
pragmas_from_env(void)
{
	char *p, *q, *var;
	const char *env = getenv("PDPMAKE_PRAGMAS");

	if (env == NULL)
		return;

	q = var = xstrdup(env);
	while ((p = gettok(&q)) != NULL)
		set_pragma(p);
	free(var);
}
#endif

/*
 * Parse input from the makefile and construct a tree structure of it.
 */
void
input(FILE *fd, int ilevel)
{
	char *p, *q, *s, *a, *str, *expanded, *copy;
	char *str1, *str2;
	struct name *np;
	struct depend *dp;
	struct cmd *cp;
	int startno, count;
	bool semicolon_cmd, seen_inference;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	uint8_t old_clevel = clevel;
	bool dbl;
	char *lib = NULL;
	glob_t gd;
	int nfile, i;
	char **files;
#else
	const bool dbl = FALSE;
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
	bool minus;
#else
	const bool minus = FALSE;
#endif

	lineno = 0;
	str1 = readline(fd, FALSE);
	while (str1) {
		str2 = NULL;

		// Newlines and comments are handled differently in command lines
		// and other types of line.  Take a copy of the current line before
		// processing it as a non-command line in case it contains a
		// rule with a command line.  That is, a line of the form:
		//
		//   target: prereq; command
		//
		copy = xstrdup(str1);
		process_line(str1);
		str = str1;

		// Check for an include line
# if ENABLE_FEATURE_MAKE_EXTENSIONS
		if (!posix)
			while (isblank(*str))
				++str;
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
		minus = !POSIX_2017 && *str == '-';
#endif
		p = str + minus;
		if (strncmp(p, "include", 7) == 0 && isblank(p[7])) {
			const char *old_makefile = makefile;
			int old_lineno = lineno;

			if (ilevel > 16)
				error("too many includes");

#if ENABLE_FEATURE_MAKE_POSIX_2024
			count = 0;
#endif
			q = expanded = expand_macros(p + 7, FALSE);
			while ((p = gettok(&q)) != NULL) {
				FILE *ifd;

#if ENABLE_FEATURE_MAKE_POSIX_2024
				++count;
				if (!POSIX_2017) {
					// Try to create include file or bring it up-to-date
					opts |= OPT_include;
					make(newname(p), 1);
					opts &= ~OPT_include;
				}
#endif
				if ((ifd = fopen(p, "r")) == NULL) {
					if (!minus)
						error("can't open include file '%s'", p);
				} else {
					makefile = p;
					input(ifd, ilevel + 1);
					fclose(ifd);
					makefile = old_makefile;
					lineno = old_lineno;
				}
#if ENABLE_FEATURE_MAKE_POSIX_2024
				if (POSIX_2017)
					break;
#endif
			}
#if ENABLE_FEATURE_MAKE_POSIX_2024
			if (POSIX_2017) {
				// In POSIX 2017 zero or more than one include file is
				// unspecified behaviour.
				if (p == NULL || gettok(&q)) {
					error("one include file per line");
				}
			} else if (count == 0) {
				// In POSIX 2024 no include file is unspecified behaviour.
# if ENABLE_FEATURE_MAKE_EXTENSIONS
				if (posix)
# endif
					error("no include file");
			}
#endif
			goto end_loop;
		}

		// Check for a macro definition
		str = str1;
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
		// POSIX 2024 seems to allow a tab as the first character of
		// a macro definition, though most implementations don't.
		if (POSIX_2017 && *str == '\t')
			error("command not allowed here");
#endif
		if (find_char(str, '=') != NULL) {
			int level = (useenv || fd == NULL) ? 4 : 3;
			// Use a copy of the line:  we might need the original
			// if this turns out to be a target rule.
			char *copy2 = xstrdup(str);
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			char *newq = NULL;
			char eq = '\0';
#endif
			q = find_char(copy2, '=');		// q can't be NULL

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			if (q - 1 > copy2) {
				switch (q[-1]) {
				case ':':
# if ENABLE_FEATURE_MAKE_POSIX_2024
					// '::=' and ':::=' are from POSIX 2024.
					if (!POSIX_2017 && q - 2 > copy2 && q[-2] == ':') {
						if (q - 3 > copy2 && q[-3] == ':') {
							eq = 'B';	// BSD-style ':='
							q[-3] = '\0';
						} else {
							eq = ':';	// GNU-style ':='
							q[-2] = '\0';
						}
						break;
					}
# endif
# if ENABLE_FEATURE_MAKE_EXTENSIONS
					// ':=' is a non-POSIX extension.
					if (posix)
						break;
					IF_FEATURE_MAKE_POSIX_2024(goto set_eq;)
# else
					break;
# endif
# if ENABLE_FEATURE_MAKE_POSIX_2024
				case '+':
				case '?':
				case '!':
					// '+=', '?=' and '!=' are from POSIX 2024.
					if (POSIX_2017)
						break;
 IF_FEATURE_MAKE_EXTENSIONS(set_eq:)
# endif
					eq = q[-1];
					q[-1] = '\0';
					break;
				}
			}
#endif
			*q++ = '\0';	// Separate name and value
			while (isblank(*q))
				q++;
			if ((p = strrchr(q, '\n')) != NULL)
				*p = '\0';

			// Expand left-hand side of assignment
			p = expanded = expand_macros(copy2, FALSE);
			if ((a = gettok(&p)) == NULL)
				error("invalid macro assignment");

			// If the expanded LHS contains ':' and ';' it can't be a
			// macro assignment but it might be a target rule.
			if ((s = strchr(a, ':')) != NULL && strchr(s, ';') != NULL) {
				free(expanded);
				free(copy2);
				goto try_target;
			}

			if (gettok(&p))
				error("invalid macro assignment");

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			if (eq == ':') {
				// GNU-style ':='.  Expand right-hand side of assignment.
				// Macro is of type immediate-expansion.
				q = newq = expand_macros(q, FALSE);
				level |= M_IMMEDIATE;
			}
# if ENABLE_FEATURE_MAKE_POSIX_2024
			else if (eq == 'B') {
				// BSD-style ':='.  Expand right-hand side of assignment,
				// though not '$$'.  Macro is of type delayed-expansion.
				q = newq = expand_macros(q, TRUE);
			} else if (eq == '?' && getmp(a) != NULL) {
				// Skip assignment if macro is already set
				goto end_loop;
			} else if (eq == '+') {
				// Append to current value
				struct macro *mp = getmp(a);
				char *rhs;
				newq = mp && mp->m_val[0] ? xstrdup(mp->m_val) : NULL;
				if (mp && mp->m_immediate) {
					// Expand right-hand side of assignment (GNU make
					// compatibility)
					rhs = expand_macros(q, FALSE);
					level |= M_IMMEDIATE;
				} else {
					rhs = q;
				}
				newq = xappendword(newq, rhs);
				if (rhs != q)
					free(rhs);
				q = newq;
			} else if (eq == '!') {
				char *cmd = expand_macros(q, FALSE);
				q = newq = run_command(cmd);
				free(cmd);
			}
# endif
#endif
			setmacro(a, q, level);
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			free(newq);
#endif
			free(copy2);
			goto end_loop;
		}

		// If we get here it must be a target rule
 try_target:
		if (*str == '\t')	// Command without target
			error("command not allowed here");
		p = expanded = expand_macros(str, FALSE);

		// Look for colon separator
		q = find_colon(p);
		if (q == NULL)
			error("expected separator");

		*q++ = '\0';	// Separate targets and prerequisites

#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// Double colon
		dbl = !posix && *q == ':';
		if (dbl)
			q++;
#endif

		// Look for semicolon separator
		cp = NULL;
		s = strchr(q, ';');
		if (s) {
			// Retrieve command from expanded copy of line
			char *copy3 = expand_macros(copy, FALSE);
			if ((p = find_colon(copy3)) && (p = strchr(p, ';')))
				cp = newcmd(process_command(p + 1), cp);
			free(copy3);
			*s = '\0';
		}
		semicolon_cmd = cp != NULL && cp->c_cmd[0] != '\0';

		// Create list of prerequisites
		dp = NULL;
		while (((p = gettok(&q)) != NULL)) {
#if !ENABLE_FEATURE_MAKE_EXTENSIONS
# if ENABLE_FEATURE_MAKE_POSIX_2024
			if (!POSIX_2017 && strcmp(p, ".WAIT") == 0)
				continue;
# endif
			np = newname(p);
			dp = newdep(np, dp);
#else
			char *newp = NULL;

			if (!posix) {
				// Allow prerequisites of form library(member1 member2).
				// Leading and trailing spaces in the brackets are skipped.
				if (!lib) {
					s = strchr(p, '(');
					if (s && !ends_with_bracket(s) && strchr(q, ')')) {
						// Looks like an unterminated archive member
						// with a terminator later on the line.
						lib = p;
						if (s[1] != '\0') {
							p = newp = xconcat3(lib, ")", "");
							s[1] = '\0';
						} else {
							continue;
						}
					}
				} else if (ends_with_bracket(p)) {
					if (*p != ')')
						p = newp = xconcat3(lib, p, "");
					lib = NULL;
					if (newp == NULL)
						continue;
				} else {
					p = newp = xconcat3(lib, p, ")");
				}
			}

			// If not in POSIX mode expand wildcards in the name.
			nfile = 1;
			files = &p;
			if (!posix && wildcard(p, &gd)) {
				nfile = gd.gl_pathc;
				files = gd.gl_pathv;
			}
			for (i = 0; i < nfile; ++i) {
# if ENABLE_FEATURE_MAKE_POSIX_2024
				if (!POSIX_2017 && strcmp(files[i], ".WAIT") == 0)
					continue;
# endif
				np = newname(files[i]);
				dp = newdep(np, dp);
			}
			if (files != &p)
				globfree(&gd);
			free(newp);
#endif /* ENABLE_FEATURE_MAKE_EXTENSIONS */
		}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		lib = NULL;
#endif

		// Create list of commands
		startno = dispno;
		while ((str2 = readline(fd, TRUE)) && *str2 == '\t') {
			cp = newcmd(process_command(str2), cp);
			free(str2);
		}
		dispno = startno;

		// Create target names and attach rule to them
		q = expanded;
		count = 0;
		seen_inference = FALSE;
		while ((p = gettok(&q)) != NULL) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			// If not in POSIX mode expand wildcards in the name.
			nfile = 1;
			files = &p;
			if (!posix && wildcard(p, &gd)) {
				nfile = gd.gl_pathc;
				files = gd.gl_pathv;
			}
			for (i = 0; i < nfile; ++i)
# define p files[i]
#endif
			{
				int ttype = target_type(p);

				np = newname(p);
				if (ttype != T_NORMAL) {
					// Enforce prerequisites/commands in POSIX mode
					if (IF_FEATURE_MAKE_EXTENSIONS(posix &&) 1) {
						if ((ttype & T_NOPREREQ) && dp)
							error_not_allowed("prerequisites", p);
						if ((ttype & T_INFERENCE)) {
							if (semicolon_cmd)
								error_in_inference_rule("'; command'");
							seen_inference = TRUE;
						}
						if ((ttype & T_COMMAND) && !cp &&
								!((ttype & T_INFERENCE) && !semicolon_cmd))
							error("commands required for %s", p);
						if (!(ttype & T_COMMAND) && cp)
							error_not_allowed("commands", p);
					}

					if ((ttype & T_INFERENCE)) {
						np->n_flag |= N_INFERENCE;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
					} else if (strcmp(p, ".DEFAULT") == 0) {
						// .DEFAULT rule is a special case
						np->n_flag |= N_SPECIAL | N_INFERENCE;
#endif
					} else {
						np->n_flag |= N_SPECIAL;
					}
				} else if (!firstname) {
					firstname = np;
				}
				addrule(np, dp, cp, dbl);
				count++;
			}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
# undef p
			if (files != &p)
				globfree(&gd);
#endif
		}
		if (IF_FEATURE_MAKE_EXTENSIONS(posix &&) seen_inference && count != 1)
			error_in_inference_rule("multiple targets");

		// Prerequisites and commands will be unused if there were
		// no targets.  Avoid leaking memory.
		if (count == 0) {
			freedeps(dp);
			freecmds(cp);
		}

 end_loop:
		free(str1);
		dispno = lineno;
		str1 = str2 ? str2 : readline(fd, FALSE);
		free(copy);
		free(expanded);
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if (!seen_first && fd) {
			if (findname(".POSIX")) {
				// The first non-comment line from a real makefile
				// defined the .POSIX special target.
				setenv("PDPMAKE_POSIXLY_CORRECT", "", 1);
				posix = TRUE;
			}
			seen_first = TRUE;
		}
#endif
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// Conditionals aren't allowed to span files
	if (clevel != old_clevel)
		error("invalid conditional");
#endif
}
/*
 * Macro control for make
 */

struct macro *macrohead[HTABSIZE];

struct macro *
getmp(const char *name)
{
	struct macro *mp;

	for (mp = macrohead[getbucket(name)]; mp; mp = mp->m_next)
		if (strcmp(name, mp->m_name) == 0)
			return mp;
	return NULL;
}

static int
is_valid_macro(const char *name)
{
	const char *s;
	for (s = name; *s; ++s) {
		// In POSIX mode only a limited set of characters are guaranteed
		// to be allowed in macro names.
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if (posix)
#endif
		{
			// Find the appropriate character set
			if (((
#if ENABLE_FEATURE_MAKE_EXTENSIONS
					(pragma & P_MACRO_NAME) ||
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
					!POSIX_2017
#else
					FALSE
#endif
				) ? !isfname(*s) : !ispname(*s)))
			return FALSE;
		}
		// As an extension allow anything that can get through the
		// input parser, apart from the following.
		if (*s == '=')
			return FALSE;
#if ENABLE_FEATURE_MAKE_POSIX_2024
		if (isblank(*s) || iscntrl(*s))
			return FALSE;
#endif
	}
	return TRUE;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
static int
potentially_valid_macro(const char *name)
{
	int ret = FALSE;

	if (!(pragma & P_MACRO_NAME)) {
		pragma |= P_MACRO_NAME;
		ret = is_valid_macro(name);
		pragma &= ~P_MACRO_NAME;
	}
	return ret;
}
#endif

void
setmacro(const char *name, const char *val, int level)
{
	struct macro *mp;
	bool valid = level & M_VALID;
	bool from_env = level & M_ENVIRON;
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
	bool immediate = level & M_IMMEDIATE;
#endif

	level &= ~(M_IMMEDIATE | M_VALID | M_ENVIRON);
	mp = getmp(name);
	if (mp) {
		// Don't replace existing macro from a lower level
		if (level > mp->m_level)
			return;

		// Replace existing macro
		free(mp->m_val);
	} else {
		// If not defined, allocate space for new
		unsigned int bucket;

		if (!valid && !is_valid_macro(name)) {
			// Silently drop invalid names from the environment
			if (from_env)
				return;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			error("invalid macro name '%s'%s", name,
					potentially_valid_macro(name) ?
					": allow with pragma macro_name" : "");
#else
			error("invalid macro name '%s'", name);
#endif
		}

		bucket = getbucket(name);
		mp = xmalloc(sizeof(struct macro));
		mp->m_next = macrohead[bucket];
		macrohead[bucket] = mp;
		mp->m_flag = FALSE;
		mp->m_name = xstrdup(name);
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
	mp->m_immediate = immediate;
#endif
	mp->m_level = level;
	mp->m_val = xstrdup(val ? val : "");
}

#if ENABLE_FEATURE_CLEAN_UP
void
freemacros(void)
{
	int i;
	struct macro *mp, *nextmp;

	for (i = 0; i < HTABSIZE; i++) {
		for (mp = macrohead[i]; mp; mp = nextmp) {
			nextmp = mp->m_next;
			free(mp->m_name);
			free(mp->m_val);
			free(mp);
		}
	}
}
#endif
/*
 * make [--posix] [-C path] [-f makefile] [-j num] [-x pragma]
 *      [-ehiknpqrsSt] [macro[:[:[:]]]=val ...] [target ...]
 *
 *  --posix  Enforce POSIX mode (non-POSIX)
 *  -C  Change directory to path (non-POSIX)
 *  -f  Makefile name
 *  -j  Number of jobs to run in parallel (not implemented)
 *  -x  Pragma to make POSIX mode less strict (non-POSIX)
 *  -e  Environment variables override macros in makefiles
 *  -h  Display help information (non-POSIX)
 *  -i  Ignore exit status
 *  -k  Continue on error
 *  -n  Pretend to make
 *  -p  Print all macros & targets
 *  -q  Question up-to-dateness of target.  Return exit status 1 if not
 *  -r  Don't use inbuilt rules
 *  -s  Make silently
 *  -S  Stop on error
 *  -t  Touch files instead of making them
 */

uint32_t opts;
const char *myname;
const char *makefile;
struct file *makefiles;
#if ENABLE_FEATURE_MAKE_POSIX_2024
char *numjobs = NULL;
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
bool posix;
bool seen_first;
unsigned char pragma = 0;
unsigned char posix_level = DEFAULT_POSIX_LEVEL;
#endif

static void
usage(int exit_code)
{
	FILE *fp = ENABLE_FEATURE_MAKE_EXTENSIONS && exit_code == 0 ?
				stdout : stderr;

	fprintf(fp,
		"Usage: %s"
		IF_FEATURE_MAKE_EXTENSIONS(" [--posix] [-C path]")
		" [-f makefile]"
		IF_FEATURE_MAKE_POSIX_2024(" [-j num]")
		IF_FEATURE_MAKE_EXTENSIONS(" [-x pragma]")
		IF_FEATURE_MAKE_EXTENSIONS("\n\t")
		IF_NOT_FEATURE_MAKE_EXTENSIONS(" [-eiknpqrsSt] ")
		IF_FEATURE_MAKE_EXTENSIONS(" [-ehiknpqrsSt] ")
		IF_NOT_FEATURE_MAKE_POSIX_2024(
			IF_FEATURE_MAKE_EXTENSIONS("[macro[:]=val ...]")
			IF_NOT_FEATURE_MAKE_EXTENSIONS("[macro=val ...]")
		)
		IF_FEATURE_MAKE_POSIX_2024(
			IF_FEATURE_MAKE_EXTENSIONS("[macro[:[:[:]]]=val ...]")
			IF_NOT_FEATURE_MAKE_EXTENSIONS("[macro[::[:]]=val ...]")
		)
		" [target ...]\n", myname);

	fprintf(fp, "\nThis build supports:"
			IF_FEATURE_MAKE_EXTENSIONS(
				" non-POSIX extensions,"
				IF_FEATURE_MAKE_POSIX_2024(" POSIX 2024,")
				" POSIX 2017\n"
			)
			IF_NOT_FEATURE_MAKE_EXTENSIONS(
				IF_FEATURE_MAKE_POSIX_2024(" POSIX 2024")
				IF_NOT_FEATURE_MAKE_POSIX_2024(" POSIX 2017")
			)
			);
#if ENABLE_FEATURE_MAKE_EXTENSIONS && ENABLE_FEATURE_MAKE_POSIX_2024
	fprintf(fp,
			"In strict POSIX mode the %s standard is enforced by default.\n",
			DEFAULT_POSIX_LEVEL == STD_POSIX_2017 ? "2017" : "2024");
#endif
#if !ENABLE_FEATURE_MAKE_EXTENSIONS
# if ENABLE_FEATURE_MAKE_POSIX_2024
	fprintf(fp, "\nFor details see:\n"
	"  https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/utilities/make.html\n");
# else
	fprintf(fp, "\nFor details see:\n"
	"  https://pubs.opengroup.org/onlinepubs/9699919799.2018edition/utilities/make.html\n");
# endif
#endif
	exit(exit_code);
}

/*
 * Process options from an argv array.  If from_env is non-zero we're
 * handling options from MAKEFLAGS so skip '-C', '-f', '-p' and '-x'.
 */
static uint32_t
process_options(int argc, char **argv, int from_env)
{
	int opt;
	uint32_t flags = 0;

	while ((opt = getopt(argc, argv, OPTSTR1 OPTSTR2 OPT_OFFSET)) != -1) {
		switch(opt) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		case 'C':
			if (!posix && !from_env) {
				if (chdir(optarg) == -1) {
					error("can't chdir to %s: %s", optarg, strerror(errno));
				}
				flags |= OPT_C;
				break;
			}
			error("-C not allowed");
			break;
#endif
		case 'f':	// Alternate file name
			if (!from_env) {
				makefiles = newfile(optarg, makefiles);
				flags |= OPT_f;
			}
			break;
		case 'e':	// Prefer env vars to macros in makefiles
			flags |= OPT_e;
			break;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		case 'h':	// Print usage message and exit
			if (posix)
				error("-h not allowed");
			usage(0);
			break;
#endif
		case 'i':	// Ignore fault mode
			flags |= OPT_i;
			break;
#if ENABLE_FEATURE_MAKE_POSIX_2024
		case 'j':
			if (!POSIX_2017) {
				const char *s;

				for (s = optarg; *s; ++s) {
					if (!isdigit(*s)) {
						usage(2);
					}
				}
				free(numjobs);
				numjobs = xstrdup(optarg);
				flags |= OPT_j;
				break;
			}
			error("-j not allowed");
			break;
#endif
		case 'k':	// Continue on error
			flags |= OPT_k;
			flags &= ~OPT_S;
			break;
		case 'n':	// Pretend mode
			flags |= OPT_n;
			break;
		case 'p':
			if (!from_env)
				flags |= OPT_p;
			break;
		case 'q':
			flags |= OPT_q;
			break;
		case 'r':
			flags |= OPT_r;
			break;
		case 't':
			flags |= OPT_t;
			break;
		case 's':	// Silent about commands
			flags |= OPT_s;
			break;
		case 'S':	// Stop on error
			flags |= OPT_S;
			flags &= ~OPT_k;
			break;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		case 'x':	// Pragma
			if (!from_env) {
				set_pragma(optarg);
				flags |= OPT_x;
			}
			break;
#endif
		default:
			if (from_env)
				error("invalid MAKEFLAGS");
			else
				usage(2);
		}
	}
	return flags;
}

/*
 * Split the contents of MAKEFLAGS into an argv array.  If the return
 * value (call it fargv) isn't NULL the caller should free fargv[1] and
 * fargv.
 */
static char **
expand_makeflags(int *fargc)
{
	const char *m, *makeflags = getenv("MAKEFLAGS");
	char *p, *argstr;
	int argc;
	char **argv;

	if (makeflags == NULL)
		return NULL;

	while (isblank(*makeflags))
		makeflags++;

	if (*makeflags == '\0')
		return NULL;

	p = argstr = xmalloc(strlen(makeflags) + 2);

	// If MAKEFLAGS doesn't start with a hyphen, doesn't look like
	// a macro definition and only contains valid option characters,
	// add a hyphen.
	argc = 3;
	if (makeflags[0] != '-' && strchr(makeflags, '=') == NULL) {
		if (strspn(makeflags, OPTSTR1 + 1) != strlen(makeflags))
			error("invalid MAKEFLAGS");
		*p++ = '-';
	} else {
		// MAKEFLAGS may need to be split, estimate size of argv array.
		for (m = makeflags; *m; ++m) {
			if (isblank(*m))
				argc++;
		}
	}

	argv = xmalloc(argc * sizeof(char *));
	argc = 0;
	argv[argc++] = (char *)myname;
	argv[argc++] = argstr;

	// Copy MAKEFLAGS into argstr, splitting at non-escaped blanks.
	m = makeflags;
	do {
		if (*m == '\\' && m[1] != '\0')
			m++;	// Skip backslash, copy next character unconditionally.
		else if (isblank(*m)) {
			// Terminate current argument and start a new one.
			*p++ = '\0';
			argv[argc++] = p;
			do {
				m++;
			} while (isblank(*m));
			continue;
		}
		*p++ = *m++;
	} while (*m != '\0');
	*p = '\0';
	argv[argc] = NULL;

	*fargc = argc;
	return argv;
}

/*
 * Instantiate all macros in an argv-style array of pointers.  Stop
 * processing at the first string that doesn't contain an equal sign.
 * As an extension, target arguments on the command line (level 1)
 * are skipped and will be processed later.
 */
static char **
process_macros(char **argv, int level)
{
	char *equal;

	for (; *argv; argv++) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
		char *colon = NULL;
		int immediate = 0;
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
		int except_dollar = FALSE;
#endif

		if (!(equal = strchr(*argv, '='))) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			// Skip targets on the command line
			if (!posix && level == 1)
				continue;
			else
#endif
				// Stop at first target
				break;
		}

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
		if (equal - 1 > *argv && equal[-1] == ':') {
# if ENABLE_FEATURE_MAKE_POSIX_2024
			if (equal - 2 > *argv && equal[-2] == ':') {
				if (POSIX_2017)
					error("invalid macro assignment");
				if (equal - 3 > *argv  && equal[-3] == ':') {
					// BSD-style ':='.  Expand RHS, but not '$$',
					// resulting macro is delayed expansion.
					colon = equal - 3;
					except_dollar = TRUE;
				} else {
					// GNU-style ':='. Expand RHS, including '$$',
					// resulting macro is immediate expansion.
					colon = equal - 2;
					immediate = M_IMMEDIATE;
				}
				*colon = '\0';
			} else
# endif
			{
# if ENABLE_FEATURE_MAKE_EXTENSIONS
				if (posix)
					error("invalid macro assignment");
				colon = equal - 1;
				immediate = M_IMMEDIATE;
				*colon = '\0';
# else
				error("invalid macro assignment");
# endif
			}
		} else
#endif
			*equal = '\0';

		/* We want to process _most_ macro assignments.
		 * There are exceptions for particular values from the
		 * environment. */
		if (!((level & M_ENVIRON) &&
				(strcmp(*argv, "MAKEFLAGS") == 0
					|| strcmp(*argv, "SHELL") == 0
#if ENABLE_FEATURE_MAKE_POSIX_2024
					|| (strcmp(*argv, "CURDIR") == 0 && !useenv && !POSIX_2017)
#endif

				))) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			if (colon) {
				char *exp = expand_macros(equal + 1, except_dollar);
				setmacro(*argv, exp, level | immediate);
				free(exp);
			} else
#endif
				setmacro(*argv, equal + 1, level);
		}

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
		if (colon)
			*colon = ':';
		else
#endif
			*equal = '=';
	}
	return argv;
}

/*
 * Update the MAKEFLAGS macro and environment variable to include any
 * command line options that don't have their default value (apart from
 * -f, -p and -S).  Also add any macros defined on the command line or
 * by the MAKEFLAGS environment variable (apart from MAKEFLAGS itself).
 * Add macros that were defined on the command line to the environment.
 */
static void
update_makeflags(void)
{
	int i;
	char optbuf[] = "-?";
	char *makeflags = NULL;
	char *macro, *s;
	const char *t;
	struct macro *mp;

	t = OPTSTR1 + 1;
	for (i = 0; *t; t++) {
#if ENABLE_FEATURE_MAKE_POSIX_2024
		if (*t == ':')
			continue;
#endif
		if ((opts & OPT_MASK & (1 << i))) {
			optbuf[1] = *t;
			makeflags = xappendword(makeflags, optbuf);
#if ENABLE_FEATURE_MAKE_POSIX_2024
			if (*t == 'j') {
				makeflags = xappendword(makeflags, numjobs);
			}
#endif
		}
		i++;
	}

	for (i = 0; i < HTABSIZE; ++i) {
		for (mp = macrohead[i]; mp; mp = mp->m_next) {
			if ((mp->m_level == 1 || mp->m_level == 2) &&
					strcmp(mp->m_name, "MAKEFLAGS") != 0) {
				macro = xmalloc(strlen(mp->m_name) + 2 * strlen(mp->m_val) + 1);
				s = stpcpy(macro, mp->m_name);
				*s++ = '=';
				for (t = mp->m_val; *t; t++) {
					if (*t == '\\' || isblank(*t))
						*s++ = '\\';
					*s++ = *t;
				}
				*s = '\0';

				makeflags = xappendword(makeflags, macro);
				free(macro);

				// Add command line macro definitions to the environment
				if (mp->m_level == 1 && strcmp(mp->m_name, "SHELL") != 0)
					setenv(mp->m_name, mp->m_val, 1);
			}
		}
	}

	if (makeflags) {
		setmacro("MAKEFLAGS", makeflags, 0);
		setenv("MAKEFLAGS", makeflags, 1);
		free(makeflags);
	}
}

static void
make_handler(int sig)
{
	signal(sig, SIG_DFL);
	remove_target();
	kill(getpid(), sig);
}

static void
init_signal(int sig)
{
	struct sigaction sa, new_action;

	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	new_action.sa_handler = make_handler;

	sigaction(sig, NULL, &sa);
	if (sa.sa_handler != SIG_IGN)
		sigaction(sig, &new_action, NULL);
}

/*
 * If the global option flag associated with a special target hasn't
 * been set mark all prerequisites of the target with a flag.  If the
 * target had no prerequisites set the global option flag.
 */
static void
mark_special(const char *special, uint32_t oflag, uint16_t nflag)
{
	struct name *np;
	struct rule *rp;
	struct depend *dp;
	int marked = FALSE;

	if (!(opts & oflag) && (np = findname(special))) {
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			for (dp = rp->r_dep; dp; dp = dp->d_next) {
				dp->d_name->n_flag |= nflag;
				marked = TRUE;
			}
		}

		if (!marked)
			opts |= oflag;
	}
}

int
main(int argc, char **argv)
{
#if ENABLE_FEATURE_MAKE_POSIX_2024
	const char *path, *newpath = NULL;
#else
	const char *path = "make";
#endif
	char **fargv, **fargv0;
	int fargc, estat;
	bool found_target;
	FILE *ifd;
	struct file *fp;

	if (argc == 0) {
		return EXIT_FAILURE;
	}

	myname = basename(*argv);
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if (argv[1] && strcmp(argv[1], "--posix") == 0) {
		argv[1] = argv[0];
		++argv;
		--argc;
		setenv("PDPMAKE_POSIXLY_CORRECT", "", 1);
		posix = TRUE;
	} else {
		posix = getenv("PDPMAKE_POSIXLY_CORRECT") != NULL;
	}
	pragmas_from_env();
#endif

#if ENABLE_FEATURE_MAKE_POSIX_2024
	if (!POSIX_2017) {
		path = argv[0];
		if (argv[0][0] != '/' && strchr(argv[0], '/')) {
			// Make relative path absolute
			path = newpath = realpath(argv[0], NULL);
			if (!path) {
				error("can't resolve path for %s: %s", argv[0], strerror(errno));
			}
		}
	} else {
		path = "make";
	}
#endif

	// Process options from MAKEFLAGS
	fargv = fargv0 = expand_makeflags(&fargc);
	if (fargv0) {
		opts = process_options(fargc, fargv, TRUE);
		fargv = fargv0 + optind;
		// Reset getopt(3) so we can call it again
		GETOPT_RESET();
	}

	// Process options from the command line
	opts |= process_options(argc, argv, FALSE);
	argv += optind;

	init_signal(SIGHUP);
	init_signal(SIGTERM);

	setmacro("$", "$", 0 | M_VALID);

#if ENABLE_FEATURE_MAKE_EXTENSIONS
	pragmas_to_env();

	// Process macro definitions from the command line
	if (!posix)
		process_macros(argv, 1);
	else
#endif
		// In POSIX mode macros must appear before targets.
		// argv should now point to targets only.
		argv = process_macros(argv, 1);

	// Process macro definitions from MAKEFLAGS
	if (fargv) {
		process_macros(fargv, 2);
		free(fargv0[1]);
		free(fargv0);
	}

	// Process macro definitions from the environment
	process_macros(environ, 3 | M_ENVIRON);

	// Update MAKEFLAGS and environment
	update_makeflags();

	// Read built-in rules
	input(NULL, 0);

	setmacro("SHELL", "/bin/sh", 4);
	setmacro("MAKE", path, 4);
#if ENABLE_FEATURE_MAKE_POSIX_2024
	if (!POSIX_2017) {
		char *cwd = NULL;
		size_t len = 0;

		do {
			len += 256;
			cwd = xrealloc(cwd, len);
			if (getcwd(cwd, len)) {
				if (!useenv) {
					// Export cwd to environment, if necessary
					char *envcwd = getenv("CURDIR");
					if (envcwd && strcmp(cwd, envcwd) != 0)
						setenv("CURDIR", cwd, 1);
				}
				setmacro("CURDIR", cwd, 4);
				break;
			}
		} while (errno == ERANGE);
		free(cwd);
	}
	free((void *)newpath);
#endif

	fp = makefiles;
	if (!fp) {	// Look for a default Makefile
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if (!posix && (ifd = fopen("PDPmakefile", "r")) != NULL)
			makefile = "PDPmakefile";
		else
#endif
		if ((ifd = fopen("makefile", "r")) != NULL)
			makefile = "makefile";
		else if ((ifd = fopen("Makefile", "r")) != NULL)
			makefile = "Makefile";
		else
			error("no makefile found");
		goto read_makefile;
	}

	while (fp) {
		if (strcmp(fp->f_name, "-") == 0) {	// Can use stdin as makefile
			ifd = stdin;
			makefile = "stdin";
		} else {
			if ((ifd = fopen(fp->f_name, "r")) == NULL)
				error("can't open %s: %s", fp->f_name, strerror(errno));
			makefile = fp->f_name;
		}
		fp = fp->f_next;
 read_makefile:
		input(ifd, 0);
		fclose(ifd);
		makefile = NULL;
	}

	if (print)
		print_details();

	mark_special(".SILENT", OPT_s, N_SILENT);
	mark_special(".IGNORE", OPT_i, N_IGNORE);
	mark_special(".PRECIOUS", OPT_precious, N_PRECIOUS);
#if ENABLE_FEATURE_MAKE_POSIX_2024
	if (!POSIX_2017)
		mark_special(".PHONY", OPT_phony, N_PHONY);
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if (posix)
#endif
	{
		// In POSIX mode only targets should now be in argv.
		found_target = FALSE;
		for (char **a = argv; *a; a++) {
			if (!strchr(*a, '='))
				found_target = TRUE;
			else if (found_target)
				error("macro assignments must precede targets");
		}
	}

	estat = 0;
	found_target = FALSE;
	for (; *argv; argv++) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// Skip macro assignments.
		if (strchr(*argv, '='))
			continue;
#endif
		found_target = TRUE;
		estat |= make(newname(*argv), 0);
	}
	if (!found_target) {
		if (!firstname)
			error("no targets defined");
		estat = make(firstname, 0);
	}

#if ENABLE_FEATURE_CLEAN_UP
# if ENABLE_FEATURE_MAKE_POSIX_2024
	free((void *)numjobs);
# endif
	freenames();
	freemacros();
	freefiles(makefiles);
#endif

	return estat & MAKE_FAILURE;
}
/*
 * Do the actual making for make
 */

struct name *target;

void
remove_target(void)
{
	if (!dryrun && !print && !precious &&
			target && !(target->n_flag & (N_PRECIOUS | N_PHONY)) &&
			unlink(target->n_name) == 0) {
		diagnostic("'%s' removed", target->n_name);
	}
}

/*
 * Update the modification time of a file to now.
 */
static void
touch(struct name *np)
{
	if (dryrun || !silent)
		printf("touch %s\n", np->n_name);

	if (!dryrun) {
		const struct timespec timebuf[2] = {{0, UTIME_NOW}, {0, UTIME_NOW}};

		if (utimensat(AT_FDCWD, np->n_name, timebuf, 0) < 0) {
			if (errno == ENOENT) {
				int fd = open(np->n_name, O_RDWR | O_CREAT, 0666);
				if (fd >= 0) {
					close(fd);
					return;
				}
			}
			warning("touch %s failed: %s\n", np->n_name, strerror(errno));
		}
	}
}

/*
 * Do commands to make a target
 */
static int
docmds(struct name *np, struct cmd *cp)
{
	int estat = 0;
	char *q, *command;

	for (; cp; cp = cp->c_next) {
		uint32_t ssilent, signore, sdomake;

		// Location of command in makefile (for use in error messages)
		makefile = cp->c_makefile;
		dispno = cp->c_dispno;
#if ENABLE_FEATURE_MAKE_POSIX_2024
		opts &= ~OPT_make;	// We want to know if $(MAKE) is expanded
#endif
		q = command = expand_macros(cp->c_cmd, FALSE);
		ssilent = silent || (np->n_flag & N_SILENT) || dotouch;
		signore = ignore || (np->n_flag & N_IGNORE);
		sdomake = (!dryrun || doinclude || domake) && !dotouch;
		for (;;) {
			if (*q == '@')	// Specific silent
				ssilent = TRUE + 1;
			else if (*q == '-')	// Specific ignore
				signore = TRUE;
			else if (*q == '+')	// Specific domake
				sdomake = TRUE + 1;
			else
				break;
			do {
				q++;
			} while (isblank(*q));
		}

		if (sdomake > TRUE) {
			// '+' must not override '@' or .SILENT
			if (ssilent != TRUE + 1 && !(np->n_flag & N_SILENT))
				ssilent = FALSE;
		} else if (!sdomake)
			ssilent = dotouch;

		if (!ssilent && *q != '\0') {	// Ignore empty commands
			puts(q);
			fflush(stdout);
		}

		if (quest && sdomake != TRUE + 1) {
			// MAKE_FAILURE means rebuild is needed
			estat |= MAKE_FAILURE | MAKE_DIDSOMETHING;
			continue;
		}

		if (sdomake && *q != '\0') {	// Ignore empty commands
			// Get the shell to execute it
			int status;
			char *cmd = !signore IF_FEATURE_MAKE_EXTENSIONS(&& posix) ?
							xconcat3("set -e;", q, "") : q;

			target = np;
			status = system(cmd);
			if (!signore IF_FEATURE_MAKE_EXTENSIONS(&& posix))
				free(cmd);
			// If this command was being run to create an include file
			// or bring it up-to-date errors should be ignored and a
			// failure status returned.
			if (status == -1 && !doinclude) {
				error("couldn't execute '%s'", q);
			} else if (status != 0 && !signore) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				if (!posix && WIFSIGNALED(status))
					remove_target();
#endif
				if (doinclude) {
					warning("failed to build '%s'", np->n_name);
				} else {
					const char *err_type = NULL;
					int err_value = 1;

					if (WIFEXITED(status)) {
						err_type = "exit";
						err_value = WEXITSTATUS(status);
					} else if (WIFSIGNALED(status)) {
						err_type = "signal";
						err_value = WTERMSIG(status);
					}

					if (!quest || err_value == 127) {
						if (err_type)
							diagnostic("failed to build '%s' %s %d",
									np->n_name, err_type, err_value);
						else
							diagnostic("failed to build '%s'", np->n_name);
					}

					if (errcont) {
						estat |= MAKE_FAILURE;
						free(command);
						break;
					}
					exit(2);
				}
			}
			target = NULL;
		}
		if (sdomake || dryrun)
			estat = MAKE_DIDSOMETHING;
		free(command);
	}

	if (dotouch && !(np->n_flag & N_PHONY) && !(estat & MAKE_DIDSOMETHING)) {
		touch(np);
		estat = MAKE_DIDSOMETHING;
	}

	makefile = NULL;
	return estat;
}

#if !ENABLE_FEATURE_MAKE_POSIX_2024
# define make1(n, c, o, a, d, i) make1(n, c, o, i)
#endif
static int
make1(struct name *np, struct cmd *cp, char *oodate, char *allsrc,
		char *dedup, struct name *implicit)
{
	char *name, *member = NULL, *base = NULL, *prereq = NULL;

	name = splitlib(np->n_name, &member);
	setmacro("?", oodate, 0 | M_VALID);
#if ENABLE_FEATURE_MAKE_POSIX_2024
	if (!POSIX_2017) {
		setmacro("+", allsrc, 0 | M_VALID);
		setmacro("^", dedup, 0 | M_VALID);
	}
#endif
	setmacro("%", member, 0 | M_VALID);
	setmacro("@", name, 0 | M_VALID);
	if (implicit IF_FEATURE_MAKE_EXTENSIONS(|| !posix)) {
		char *s;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// As an extension, if we're not dealing with an implicit
		// rule set $< to the first out-of-date prerequisite.
		if (implicit == NULL) {
			if (oodate) {
				s = strchr(oodate, ' ');
				if (s)
					*s = '\0';
				prereq = oodate;
			}
		} else
#endif
			prereq = implicit->n_name;

		base = member ? member : name;
		s = suffix(base);
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// As an extension, if we're not dealing with an implicit
		// rule and the target ends with a known suffix, remove it
		// and set $* to the stem, else to an empty string.
		if (implicit == NULL && !is_suffix(s))
			base = NULL;
		else
#endif
			*s = '\0';
	}
	setmacro("<", prereq, 0 | M_VALID);
	setmacro("*", base, 0 | M_VALID);
	free(name);

	return docmds(np, cp);
}

/*
 * Determine if the modification time of a target, t, is less than
 * that of a prerequisite, p.  If the tv_nsec member of either is
 * exactly 0 we assume (possibly incorrectly) that the time resolution
 * is 1 second and only compare tv_sec values.
 */
static int
timespec_le(const struct timespec *t, const struct timespec *p)
{
	if (t->tv_nsec == 0 || p->tv_nsec == 0)
		return t->tv_sec <= p->tv_sec;
	else if (t->tv_sec < p->tv_sec)
		return TRUE;
	else if (t->tv_sec == p->tv_sec)
		return t->tv_nsec <= p->tv_nsec;
	return FALSE;
}

/*
 * Return the greater of two struct timespecs
 */
static const struct timespec *
timespec_max(const struct timespec *t, const struct timespec *p)
{
	return timespec_le(t, p) ? p : t;
}

/*
 * Recursive routine to make a target.
 */
int
make(struct name *np, int level)
{
	struct depend *dp;
	struct rule *rp;
	struct name *impdep = NULL;	// implicit prerequisite
	struct rule imprule;
	struct cmd *sc_cmd = NULL;	// commands for single-colon rule
	char *oodate = NULL;
#if ENABLE_FEATURE_MAKE_POSIX_2024
	char *allsrc = NULL;
	char *dedup = NULL;
#endif
	struct timespec dtim = {1, 0};
	int estat = 0;

	if (np->n_flag & N_DONE)
		return 0;
	if (np->n_flag & N_DOING)
		error("circular dependency for %s", np->n_name);
	np->n_flag |= N_DOING;

	if (!np->n_tim.tv_sec)
		modtime(np);		// Get modtime of this file

	if (!(np->n_flag & N_DOUBLE)) {
		// Find the commands needed for a single-colon rule, using
		// an inference rule or .DEFAULT rule if necessary (but,
		// as an extension, not for phony targets)
		sc_cmd = getcmd(np);
		if (!sc_cmd
#if ENABLE_FEATURE_MAKE_EXTENSIONS && ENABLE_FEATURE_MAKE_POSIX_2024
				&& (posix || !(np->n_flag & N_PHONY))
#endif
				) {
			impdep = dyndep(np, &imprule);
			if (impdep) {
				sc_cmd = imprule.r_cmd;
				addrule(np, imprule.r_dep, NULL, FALSE);
			}
		}

		// As a last resort check for a default rule
		if (!(np->n_flag & N_TARGET) && np->n_tim.tv_sec == 0) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS && ENABLE_FEATURE_MAKE_POSIX_2024
			if (posix || !(np->n_flag & N_PHONY))
#endif
				sc_cmd = getcmd(findname(".DEFAULT"));
			if (!sc_cmd) {
				if (doinclude)
					return 1;
				error("don't know how to make %s", np->n_name);
			}
			impdep = np;
		}
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	else {
		// If any double-colon rule has no commands we need
		// an inference rule (but, as an extension, not for phony targets)
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			if (!rp->r_cmd) {
# if ENABLE_FEATURE_MAKE_POSIX_2024
				if (posix || !(np->n_flag & N_PHONY))
# endif
					impdep = dyndep(np, &imprule);
				if (!impdep) {
					if (doinclude)
						return 1;
					error("don't know how to make %s", np->n_name);
				}
				break;
			}
		}
	}
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
	// Reset flag to detect duplicate prerequisites
	if (!(np->n_flag & N_DOUBLE)) {
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			for (dp = rp->r_dep; dp; dp = dp->d_next) {
				dp->d_name->n_flag &= ~N_MARK;
			}
		}
	}
#endif

	for (rp = np->n_rule; rp; rp = rp->r_next) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		struct name *locdep = NULL;

		// Each double-colon rule is handled separately.
		if ((np->n_flag & N_DOUBLE)) {
			// If the rule has no commands use the inference rule.
			if (!rp->r_cmd) {
				locdep = impdep;
				imprule.r_dep->d_next = rp->r_dep;
				rp->r_dep = imprule.r_dep;
				rp->r_cmd = imprule.r_cmd;
			}
			// A rule with no prerequisities is executed unconditionally.
			if (!rp->r_dep)
				dtim = np->n_tim;
			// Reset flag to detect duplicate prerequisites
			for (dp = rp->r_dep; dp; dp = dp->d_next) {
				dp->d_name->n_flag &= ~N_MARK;
			}
		}
#endif
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Make prerequisite
			estat |= make(dp->d_name, level + 1);

			// Make strings of out-of-date prerequisites (for $?),
			// all prerequisites (for $+) and deduplicated prerequisites
			// (for $^).
			if (timespec_le(&np->n_tim, &dp->d_name->n_tim)) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				if (posix || !(dp->d_name->n_flag & N_MARK))
#endif
					oodate = xappendword(oodate, dp->d_name->n_name);
			}
#if ENABLE_FEATURE_MAKE_POSIX_2024
			allsrc = xappendword(allsrc, dp->d_name->n_name);
			if (!(dp->d_name->n_flag & N_MARK))
				dedup = xappendword(dedup, dp->d_name->n_name);
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			dp->d_name->n_flag |= N_MARK;
#endif
			dtim = *timespec_max(&dtim, &dp->d_name->n_tim);
		}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if ((np->n_flag & N_DOUBLE)) {
			if (((np->n_flag & N_PHONY) || timespec_le(&np->n_tim, &dtim))) {
				if (!(estat & MAKE_FAILURE)) {
					estat |= make1(np, rp->r_cmd, oodate, allsrc,
										dedup, locdep);
					dtim = (struct timespec){1, 0};
				}
				free(oodate);
				oodate = NULL;
			}
#if ENABLE_FEATURE_MAKE_POSIX_2024
			free(allsrc);
			free(dedup);
			allsrc = dedup = NULL;
#endif
			if (locdep) {
				rp->r_dep = rp->r_dep->d_next;
				rp->r_cmd = NULL;
			}
		}
#endif
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if ((np->n_flag & N_DOUBLE) && impdep)
		free(imprule.r_dep);
#endif

	np->n_flag |= N_DONE;
	np->n_flag &= ~N_DOING;

	if (!(np->n_flag & N_DOUBLE) &&
				((np->n_flag & N_PHONY) || (timespec_le(&np->n_tim, &dtim)))) {
		if (!(estat & MAKE_FAILURE)) {
			if (sc_cmd)
				estat |= make1(np, sc_cmd, oodate, allsrc, dedup, impdep);
			else if (!doinclude && level == 0 && !(estat & MAKE_DIDSOMETHING))
				warning("nothing to be done for %s", np->n_name);
		} else if (!doinclude && !quest) {
			diagnostic("'%s' not built due to errors", np->n_name);
		}
		free(oodate);
	}

	if (estat & MAKE_DIDSOMETHING) {
		modtime(np);
		if (!np->n_tim.tv_sec)
			clock_gettime(CLOCK_REALTIME, &np->n_tim);
	} else if (!quest && level == 0 && !timespec_le(&np->n_tim, &dtim))
		printf("%s: '%s' is up to date\n", myname, np->n_name);

#if ENABLE_FEATURE_MAKE_POSIX_2024
	free(allsrc);
	free(dedup);
#endif
	return estat;
}
/*
 * Get modification time of file or archive member
 */
#include <ar.h>

/*
 * Read a number from an archive header.
 */
static size_t
argetnum(const char *str, int len)
{
	const char *s;
	size_t val = 0;

	for (s = str; s < str + len && isdigit(*s); s++) {
		// Half-hearted attempt to avoid overflow
		if (val > (INT_MAX - 1)/10)
			break;
		val = val * 10 + *s - '0';
	}
	if (s != str + len && *s != ' ')
		error("invalid archive");
	return val;
}

/*
 * Search an archive for the given member and return its timestamp or 0.
 * This code assumes System V/GNU archive format.
 */
static time_t
arsearch(FILE *fd, const char *member)
{
	struct ar_hdr hdr;
	char *s, *t, *names = NULL;
	size_t len, offset, max_offset = 0;
	time_t mtime = 0;

	do {
 top:
		len = fread(&hdr, 1, sizeof(hdr), fd);
		if (len < sizeof(hdr) ||
				memcmp(hdr.ar_fmag, ARFMAG, sizeof(hdr.ar_fmag)) != 0) {
			if (feof(fd))
				break;
			error("invalid archive");
		}

		// Get length of this member.  Length in the file is padded
		// to an even number of bytes.
		len = argetnum(hdr.ar_size, sizeof(hdr.ar_size));
		if (len % 2 == 1)
			len++;

		t = hdr.ar_name;
		if (hdr.ar_name[0] == '/') {
			if (hdr.ar_name[1] == ' ') {
				// Skip symbol table
				continue;
			} else if (hdr.ar_name[1] == '/' && names == NULL) {
				// Save list of extended filenames for later use
				names = xmalloc(len);
				if (fread(names, 1, len, fd) != len)
					error("invalid archive");
				// Replace newline separators with NUL
				for (s = names; s < names + len; s++) {
					if (*s == '\n')
						*s = '\0';
				}
				max_offset = len;
				goto top;
			} else if (isdigit(hdr.ar_name[1]) && names) {
				// An extended filename, get its offset in the names list
				offset = argetnum(hdr.ar_name + 1, sizeof(hdr.ar_name) - 1);
				if (offset > max_offset)
					error("invalid archive");
				t = names + offset;
			} else {
				error("invalid archive");
			}
		}

		s = strchr(t, '/');
		if (s == NULL)
			error("invalid archive");
		*s = '\0';

		if (strcmp(t, member) == 0) {
			mtime = argetnum(hdr.ar_date, sizeof(hdr.ar_date));
			break;
		}
	} while (fseek(fd, len, SEEK_CUR) == 0);
	free(names);
	return mtime;
}

static time_t
artime(const char *archive, const char *member)
{
	FILE *fd;
	char magic[SARMAG];
	size_t len;
	time_t mtime;

	fd = fopen(archive, "r");
	if (fd == NULL)
		return 0;

	len = fread(magic, 1, sizeof(magic), fd);
	if (len < sizeof(magic) || memcmp(magic, ARMAG, SARMAG) != 0)
		error("%s: not an archive", archive);

	mtime = arsearch(fd, member);
	fclose(fd);
	return mtime;
}

/*
 * If the name is of the form 'libname(member.o)' split it into its
 * name and member parts and set the member pointer to point to the
 * latter.  Otherwise just take a copy of the name and don't alter
 * the member pointer.
 *
 * In either case the return value is an allocated string which must
 * be freed by the caller.
 */
char *
splitlib(const char *name, char **member)
{
	char *s, *t;
	size_t len;

	t = xstrdup(name);
	s = strchr(t, '(');
	if (s) {
		// We have 'libname(member.o)'
		*s++ = '\0';
		len = strlen(s);
		if (len <= 1 || s[len - 1] != ')' || *t == '\0')
			error("invalid name '%s'", name);
		s[len - 1] = '\0';
		*member = s;
	}
	return t;
}

/*
 * Get the modification time of a file.  Set it to 0 if the file
 * doesn't exist.
 */
void
modtime(struct name *np)
{
	char *name, *member = NULL;
	struct stat info;

	name = splitlib(np->n_name, &member);
	if (member) {
		// Looks like library(member)
		np->n_tim.tv_sec = artime(name, member);
		np->n_tim.tv_nsec = 0;
	} else if (stat(name, &info) < 0) {
		if (errno != ENOENT)
			error("can't open %s: %s", name, strerror(errno));
		np->n_tim.tv_sec = 0;
		np->n_tim.tv_nsec = 0;
	} else {
		np->n_tim.tv_sec = info.st_mtim.tv_sec;
		np->n_tim.tv_nsec = info.st_mtim.tv_nsec;
	}
	free(name);
}
/*
 * Control of the implicit suffix rules
 */

/*
 * Return a pointer to the suffix of a name (which may be the
 * terminating NUL if there's no suffix).
 */
char *
suffix(const char *name)
{
	char *p = strrchr(name, '.');
	return p ? p : (char *)name + strlen(name);
}

/*
 * Find a name structure whose name is formed by concatenating two
 * strings.  If 'create' is TRUE the name is created if necessary.
 */
static struct name *
namecat(const char *s, const char *t, int create)
{
	char *p;
	struct name *np;

	p = xconcat3(s, t, "");
	np = create ? newname(p) : findname(p);
	free(p);
	return np;
}

/*
 * Dynamic dependency.  This routine applies the suffix rules
 * to try and find a source and a set of rules for a missing
 * target.  NULL is returned on failure.  On success the name of
 * the implicit prerequisite is returned and the details are
 * placed in the imprule structure provided by the caller.
 */
struct name *
dyndep(struct name *np, struct rule *imprule)
{
	char *suff, *newsuff;
	char *base, *name, *member;
	struct name *xp;		// Suffixes
	struct name *sp;		// Suffix rule
	struct name *pp = NULL;	// Implicit prerequisite
	struct rule *rp;
	struct depend *dp;
	IF_NOT_FEATURE_MAKE_EXTENSIONS(const) bool chain = FALSE;

	member = NULL;
	name = splitlib(np->n_name, &member);

	suff = xstrdup(suffix(name));
	base = member ? member : name;
	*suffix(base) = '\0';

	xp = newname(".SUFFIXES");
#if ENABLE_FEATURE_MAKE_EXTENSIONS
 retry:
#endif
	for (rp = xp->n_rule; rp; rp = rp->r_next) {
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Generate new suffix rule to try
			newsuff = dp->d_name->n_name;
			sp = namecat(newsuff, suff, FALSE);
			if (sp && sp->n_rule) {
				struct name *ip;
				int got_ip;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
				// Has rule already been used in this chain?
				if ((sp->n_flag & N_MARK))
					continue;
#endif
				// Generate a name for an implicit prerequisite
				ip = namecat(base, newsuff, TRUE);
				if ((ip->n_flag & N_DOING))
					continue;

				if (!ip->n_tim.tv_sec)
					modtime(ip);

				if (!chain) {
					got_ip = ip->n_tim.tv_sec || (ip->n_flag & N_TARGET);
				}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				else {
					sp->n_flag |= N_MARK;
					got_ip = dyndep(ip, NULL) != NULL;
					sp->n_flag &= ~N_MARK;
				}
#endif

				if (got_ip) {
					// Prerequisite exists or we know how to make it
					if (imprule) {
						imprule->r_dep = newdep(ip, NULL);
						imprule->r_cmd = sp->n_rule->r_cmd;
					}
					pp = ip;
					goto finish;
				}
			}
		}
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// If we didn't find an existing file or an explicit rule try
	// again, this time looking for a chained inference rule.
	if (!posix && !chain) {
		chain = TRUE;
		goto retry;
	}
#endif
 finish:
	free(suff);
	free(name);
	return pp;
}

#define RULES \
	".c.o:\n" \
	"	$(CC) $(CFLAGS) -c $<\n" \
	".y.o:\n" \
	"	$(YACC) $(YFLAGS) $<\n" \
	"	$(CC) $(CFLAGS) -c y.tab.c\n" \
	"	rm -f y.tab.c\n" \
	"	mv y.tab.o $@\n" \
	".y.c:\n" \
	"	$(YACC) $(YFLAGS) $<\n" \
	"	mv y.tab.c $@\n" \
	".l.o:\n" \
	"	$(LEX) $(LFLAGS) $<\n" \
	"	$(CC) $(CFLAGS) -c lex.yy.c\n" \
	"	rm -f lex.yy.c\n" \
	"	mv lex.yy.o $@\n" \
	".l.c:\n" \
	"	$(LEX) $(LFLAGS) $<\n" \
	"	mv lex.yy.c $@\n" \
	".c.a:\n" \
	"	$(CC) -c $(CFLAGS) $<\n" \
	"	$(AR) $(ARFLAGS) $@ $*.o\n" \
	"	rm -f $*.o\n" \
	".c:\n" \
	"	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<\n" \
	".sh:\n" \
	"	cp $< $@\n" \
	"	chmod a+x $@\n"

#define RULES_2017 \
	".SUFFIXES:.o .c .y .l .a .sh .f\n" \
	".f.o:\n" \
	"	$(FC) $(FFLAGS) -c $<\n" \
	".f.a:\n" \
	"	$(FC) -c $(FFLAGS) $<\n" \
	"	$(AR) $(ARFLAGS) $@ $*.o\n" \
	"	rm -f $*.o\n" \
	".f:\n" \
	"	$(FC) $(FFLAGS) $(LDFLAGS) -o $@ $<\n"

#define RULES_2024 \
	".SUFFIXES:.o .c .y .l .a .sh\n"

#define MACROS \
	"CFLAGS=-O1\n" \
	"YACC=yacc\n" \
	"YFLAGS=\n" \
	"LEX=lex\n" \
	"LFLAGS=\n" \
	"AR=ar\n" \
	"ARFLAGS=-rv\n" \
	"LDFLAGS=\n"

#define MACROS_2017 \
	"CC=c99\n" \
	"FC=fort77\n" \
	"FFLAGS=-O1\n" \

#define MACROS_2024 \
	"CC=c17\n"

#define MACROS_EXT \
	"CC=cc\n"

/*
 * Read the built-in rules using a fake fgets-like interface.
 */
char *
getrules(char *s, int size)
{
	char *r = s;
	static const char *rulepos = NULL;
	static int rule_idx = 0;

	if (rulepos == NULL || *rulepos == '\0') {
		if (rule_idx == 0) {
			rulepos = MACROS;
			rule_idx++;
		} else if (rule_idx == 1) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			if (POSIX_2017)
				rulepos = MACROS_2017;
			else if (posix)
				rulepos = MACROS_2024;
			else
				rulepos = MACROS_EXT;
#elif ENABLE_FEATURE_MAKE_POSIX_2024
			rulepos = MACROS_2024;
#else
			rulepos = MACROS_2017;
#endif
			rule_idx++;
		} else if (!norules) {
			if (rule_idx == 2) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				rulepos = POSIX_2017 ? RULES_2017 : RULES_2024;
#elif ENABLE_FEATURE_MAKE_POSIX_2024
				rulepos = RULES_2024;
#else
				rulepos = RULES_2017;
#endif
				rule_idx++;
			} else if (rule_idx == 3) {
				rulepos = RULES;
				rule_idx++;
			}
		}
	}

	if (*rulepos == '\0')
		return NULL;

	while (--size) {
		if ((*r++ = *rulepos++) == '\n')
			break;
	}
	*r = '\0';
	return s;
}
/*
 * Process name, rule, command and prerequisite structures
 */

/*
 * Add a prerequisite to the end of the supplied list.
 * Return the new head pointer for that list.
 */
struct depend *
newdep(struct name *np, struct depend *dphead)
{
	struct depend *dpnew;
	struct depend *dp;

	dpnew = xmalloc(sizeof(struct depend));
	dpnew->d_next = NULL;
	dpnew->d_name = np;
	dpnew->d_refcnt = 0;

	if (dphead == NULL)
		return dpnew;

	for (dp = dphead; dp->d_next; dp = dp->d_next)
		;

	dp->d_next = dpnew;

	return dphead;
}

void
freedeps(struct depend *dp)
{
	struct depend *nextdp;

	if (dp && --dp->d_refcnt <= 0) {
		for (; dp; dp = nextdp) {
			nextdp = dp->d_next;
			free(dp);
		}
	}
}

/*
 * Add a command to the end of the supplied list of commands.
 * Return the new head pointer for that list.
 */
struct cmd *
newcmd(char *str, struct cmd *cphead)
{
	struct cmd *cpnew;
	struct cmd *cp;

	while (isspace(*str))
		str++;

	cpnew = xmalloc(sizeof(struct cmd));
	cpnew->c_next = NULL;
	cpnew->c_cmd = xstrdup(str);
	cpnew->c_refcnt = 0;
	cpnew->c_makefile = makefile ? xstrdup(makefile) : NULL;
	cpnew->c_dispno = dispno;

	if (cphead == NULL)
		return cpnew;

	for (cp = cphead; cp->c_next; cp = cp->c_next)
		;

	cp->c_next = cpnew;

	return cphead;
}

void
freecmds(struct cmd *cp)
{
	struct cmd *nextcp;

	if (cp && --cp->c_refcnt <= 0) {
		for (; cp; cp = nextcp) {
			nextcp = cp->c_next;
			free(cp->c_cmd);
			free((void *)cp->c_makefile);
			free(cp);
		}
	}
}

struct name *namehead[HTABSIZE];
struct name *firstname;

struct name *
findname(const char *name)
{
	struct name *np;

	for (np = namehead[getbucket(name)]; np; np = np->n_next) {
		if (strcmp(name, np->n_name) == 0)
			return np;
	}
	return NULL;
}

static int
check_name(const char *name)
{
	const char *s;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
# if defined(__CYGWIN__)
	if (!posix || (pragma & P_WINDOWS)) {
		if (isalpha(name[0]) && name[1] == ':' && name[2] == '/') {
			name += 3;
		}
	}
# endif
	if (!posix) {
		for (s = name; *s; ++s) {
			if (*s == '=')
				return FALSE;
		}
		return TRUE;
	}
#endif

	for (s = name; *s; ++s) {
		if ((
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			(pragma & P_TARGET_NAME) ||
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
				!POSIX_2017
#else
				FALSE
#endif
			) ? !(isfname(*s) || *s == '/') : !ispname(*s))
			return FALSE;
	}
	return TRUE;
}

int
is_valid_target(const char *name)
{
	char *archive, *member = NULL;
	int ret;

	/* Names of the form 'lib(member)' are referred to as 'expressions'
	 * in POSIX and are subjected to special treatment.  The 'lib'
	 * and 'member' elements must each be a valid target name. */
	archive = splitlib(name, &member);
	ret = check_name(archive) && (member == NULL || check_name(member));
	free(archive);

	return ret;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
static int
potentially_valid_target(const char *name)
{
	int ret = FALSE;

	if (!(pragma & P_TARGET_NAME)) {
		pragma |= P_TARGET_NAME;
		ret = is_valid_target(name);
		pragma &= ~P_TARGET_NAME;
	}
	return ret;
}
#endif

/*
 * Intern a name.  Return a pointer to the name struct
 */
struct name *
newname(const char *name)
{
	struct name *np = findname(name);

	if (np == NULL) {
		unsigned int bucket;

		if (!is_valid_target(name))
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			error("invalid target name '%s'%s", name,
					potentially_valid_target(name) ?
						": allow with pragma target_name" : "");
#else
			error("invalid target name '%s'", name);
#endif

		bucket = getbucket(name);
		np = xmalloc(sizeof(struct name));
		np->n_next = namehead[bucket];
		namehead[bucket] = np;
		np->n_name = xstrdup(name);
		np->n_rule = NULL;
		np->n_tim = (struct timespec){0, 0};
		np->n_flag = 0;
	}
	return np;
}

/*
 * Return the commands on the first rule that has them or NULL.
 */
struct cmd *
getcmd(struct name *np)
{
	struct rule *rp;

	if (np == NULL)
		return NULL;

	for (rp = np->n_rule; rp; rp = rp->r_next)
		if (rp->r_cmd)
			return rp->r_cmd;
	return NULL;
}

#if ENABLE_FEATURE_CLEAN_UP
void
freenames(void)
{
	int i;
	struct name *np, *nextnp;

	for (i = 0; i < HTABSIZE; i++) {
		for (np = namehead[i]; np; np = nextnp) {
			nextnp = np->n_next;
			free(np->n_name);
			freerules(np->n_rule);
			free(np);
		}
	}
}
#endif

void
freerules(struct rule *rp)
{
	struct rule *nextrp;

	for (; rp; rp = nextrp) {
		nextrp = rp->r_next;
		freedeps(rp->r_dep);
		freecmds(rp->r_cmd);
		free(rp);
	}
}

static void *
inc_ref(void *vp)
{
	if (vp) {
		struct depend *dp = vp;
		if (dp->d_refcnt == INT_MAX)
			error("out of memory");
		dp->d_refcnt++;
	}
	return vp;
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
// Order must match constants in make.h
// POSIX levels must be last and in increasing order
static const char *p_name[] = {
	"macro_name",
	"target_name",
	"command_comment",
	"empty_suffix",
#if defined(__CYGWIN__)
	"windows",
#endif
	"posix_2017",
	"posix_2024",
	"posix_202x"
};

void
set_pragma(const char *name)
{
	int i;

	// posix_202x is an alias for posix_2024
	for (i = 0; i < sizeof(p_name)/sizeof(p_name[0]); ++i) {
		if (strcmp(name, p_name[i]) == 0) {
#if !ENABLE_FEATURE_MAKE_POSIX_2024
			if (i == BIT_POSIX_2024 || i == BIT_POSIX_202X) {
				break;
			}
#endif
			if (i >= BIT_POSIX_2017) {
				// POSIX level is stored in a separate variable.
				// No bits in 'pragma' are used.
				if (posix_level == DEFAULT_POSIX_LEVEL) {
					posix_level = i - BIT_POSIX_2017;
					if (posix_level > STD_POSIX_2024)
						posix_level = STD_POSIX_2024;
				} else if (posix_level != i - BIT_POSIX_2017)
					warning("unable to change POSIX level");
			} else {
				pragma |= 1 << i;
			}
			return;
		}
	}
	warning("invalid pragma '%s'", name);
}

void
pragmas_to_env(void)
{
	int i;
	char *val = NULL;

	for (i = 0; i < BIT_POSIX_2017; ++i) {
		if ((pragma & (1 << i)))
			val = xappendword(val, p_name[i]);
	}

	if (posix_level != DEFAULT_POSIX_LEVEL)
		val = xappendword(val, p_name[BIT_POSIX_2017 + posix_level]);

	if (val) {
		setenv("PDPMAKE_PRAGMAS", val, 1);
		free(val);
	}
}
#endif

/*
 * Add a new rule to a target.  This checks to see if commands already
 * exist for the target.  If flag is TRUE the target can have multiple
 * rules with commands (double-colon rules).
 *
 * i)  If the name is a special target and there are no prerequisites
 *     or commands to be added remove all prerequisites and commands.
 *     This is necessary when clearing a built-in inference rule.
 * ii) If name is a special target and has commands, replace them.
 *     This is for redefining commands for an inference rule.
 */
void
addrule(struct name *np, struct depend *dp, struct cmd *cp, int flag)
{
	struct rule *rp;
	struct rule **rpp;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// Can't mix single-colon and double-colon rules
	if (!posix && (np->n_flag & N_TARGET)) {
		if (!(np->n_flag & N_DOUBLE) != !flag)		// like xor
			error("inconsistent rules for target %s", np->n_name);
	}
#endif

	// Clear out prerequisites and commands
	if ((np->n_flag & N_SPECIAL) && !dp && !cp) {
#if ENABLE_FEATURE_MAKE_POSIX_2024
		if (strcmp(np->n_name, ".PHONY") == 0)
			return;
#endif
		freerules(np->n_rule);
		np->n_rule = NULL;
		return;
	}

	if (cp && !(np->n_flag & N_DOUBLE) && getcmd(np)) {
		// Handle the inference rule redefinition case
		// .DEFAULT rule can also be redefined (as an extension).
		if ((np->n_flag & N_INFERENCE)
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				&& !(posix && (np->n_flag & N_SPECIAL))
#endif
		) {
			freerules(np->n_rule);
			np->n_rule = NULL;
		} else {
			error("commands defined twice for target %s", np->n_name);
		}
	}

	rpp = &np->n_rule;
	while (*rpp)
		rpp = &(*rpp)->r_next;

	*rpp = rp = xmalloc(sizeof(struct rule));
	rp->r_next = NULL;
	rp->r_dep = inc_ref(dp);
	rp->r_cmd = inc_ref(cp);

	np->n_flag |= N_TARGET;
	if (flag)
		np->n_flag |= N_DOUBLE;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if (strcmp(np->n_name, ".PRAGMA") == 0) {
		for (; dp; dp = dp->d_next) {
			set_pragma(dp->d_name->n_name);
		}
		pragmas_to_env();
	}
#endif
}
/*
 * Utility functions.
 */

/*
 * Print message, with makefile and line number if possible.
 */
static void
vwarning(FILE *stream, const char *msg, va_list list)
{
	if (makefile)
		fprintf(stream, "%s: (%s:%d): ", myname, makefile, dispno);
	else
		fprintf(stream, "%s: ", myname);
	vfprintf(stream, msg, list);
	fputc('\n', stream);
}

/*
 * Diagnostic handler.  Print message to standard error.
 */
void
diagnostic(const char *msg, ...)
{
	va_list list;

	va_start(list, msg);
	vwarning(stderr, msg, list);
	va_end(list);
}

/*
 * Error handler.  Print message and exit.
 */
void
error(const char *msg, ...)
{
	va_list list;

	va_start(list, msg);
	vwarning(stderr, msg, list);
	va_end(list);
	exit(2);
}

#if ENABLE_FEATURE_MAKE_EXTENSIONS
void
error_unexpected(const char *s)
{
	error("unexpected %s", s);
}
#endif

void
error_in_inference_rule(const char *s)
{
	error("%s in inference rule", s);
}

void
error_not_allowed(const char *s, const char *t)
{
	error("%s not allowed for %s", s, t);
}

void
warning(const char *msg, ...)
{
	va_list list;

	va_start(list, msg);
	vwarning(stdout, msg, list);
	va_end(list);
}

void *
xmalloc(size_t len)
{
	void *ret = malloc(len);
	if (ret == NULL)
		error("out of memory");
	return ret;
}

void *
xrealloc(void *ptr, size_t len)
{
	void *ret = realloc(ptr, len);
	if (ret == NULL)
		error("out of memory");
	return ret;
}

char *
xconcat3(const char *s1, const char *s2, const char *s3)
{
	const size_t len1 = strlen(s1);
	const size_t len2 = strlen(s2);
	const size_t len3 = strlen(s3);

	char *t = xmalloc(len1 + len2 + len3 + 1);
	char *s = t;

	s = (char *)memcpy(s, s1, len1) + len1;
	s = (char *)memcpy(s, s2, len2) + len2;
	s = (char *)memcpy(s, s3, len3) + len3;
	*s = '\0';

	return t;
}

char *
xstrdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *t = xmalloc(len);
	return memcpy(t, s, len);
}

char *
xstrndup(const char *s, size_t n)
{
	char *t = strndup(s, n);
	if (t == NULL)
		error("out of memory");
	return t;
}

/*
 * Append a word to a space-separated string of words.  The first
 * call should use a NULL pointer for str, subsequent calls should
 * pass an allocated string which will be freed.
 */
char *
xappendword(const char *str, const char *word)
{
	char *newstr = str ? xconcat3(str, " ", word) : xstrdup(word);
	free((void *)str);
	return newstr;
}

unsigned int
getbucket(const char *name)
{
	unsigned int hashval = 0;
	const unsigned char *p = (unsigned char *)name;

	while (*p)
		hashval ^= (hashval << 5) + (hashval >> 2) + *p++;
	return hashval % HTABSIZE;
}

/*
 * Add a file to the end of the supplied list of files.
 * Return the new head pointer for that list.
 */
struct file *
newfile(char *str, struct file *fphead)
{
	struct file *fpnew;
	struct file *fp;

	fpnew = xmalloc(sizeof(struct file));
	fpnew->f_next = NULL;
	fpnew->f_name = xstrdup(str);

	if (fphead == NULL)
		return fpnew;

	for (fp = fphead; fp->f_next; fp = fp->f_next)
		;

	fp->f_next = fpnew;

	return fphead;
}

#if ENABLE_FEATURE_CLEAN_UP
void
freefiles(struct file *fp)
{
	struct file *nextfp;

	for (; fp; fp = nextfp) {
		nextfp = fp->f_next;
		free(fp->f_name);
		free(fp);
	}
}
#endif
