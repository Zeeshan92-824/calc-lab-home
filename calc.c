// Zeeshan Kamal 251ADB053
// https://github.com/Zeeshan92-824/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used:  <every AI tool, website and book you used; details in comments where you used them>

// -std=c17 hides POSIX functions (mkdir, stat); this macro turns them back on.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NAME   "zeeshan"
#define LAST   "kamal"
#define ID     "251ADB053"

// Reads the whole file into ONE buffer. Doubling the capacity means each byte
// is copied O(1) times on average, so multi-megabyte files stay fast.
static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got;
    while ((got = fread(buf + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap + 1);
            if (!tmp) { free(buf); fclose(f); return NULL; }
            buf = tmp;
        }
    }
    fclose(f);
    buf[n] = '\0';          // sentinel: the parser can stop at '\0'
    *len = n;
    return buf;
}

// ---- Parser state -----------------------------------------------------------
// One cursor into the whole-file buffer. Because the buffer holds the entire
// file, "position" is always (p - start + 1): newlines and comment lines are
// counted automatically, with no separate counter that could drift out of sync.
static const char *start;   // first byte of the file
static const char *p;       // the character we are looking at right now
static size_t err_pos = 0;  // 0 = no error so far; otherwise the 1-based error position

// Only the FIRST error counts (rule 3), so later calls are ignored. The caller
// passes the token the parser was looking at when it could not continue (rule 5).
static void fail_at(const char *where) {
    if (!err_pos) err_pos = (size_t)(where - start) + 1;
}

static void skip_ws(void) {                 // spaces/tabs may sit between any tokens
    while (*p == ' ' || *p == '\t') p++;
}

// ---- Values: integer or float ----------------------------------------------
// Every value remembers which kind it is. The rule (rule 9) is: an operation is
// done in double if EITHER operand is a float, otherwise in long long. Carrying
// the kind along with the number lets each operator make that decision locally,
// at the moment it has both operands, instead of guessing from the text.
typedef struct { int is_float; long long i; double d; } Val;

static Val mk_int(long long i) { Val v = { 0, i, 0.0 }; return v; }
static Val mk_flt(double d)    { Val v = { 1, 0, d };   return v; }
static double as_dbl(Val v)    { return v.is_float ? v.d : (double)v.i; }

// A float result that is infinite or NaN is an error reported at the OPERATOR
// (rule 5), so the caller passes the operator's position, not the operands'.
static Val check_float(double d, const char *op) {
    if (!isfinite(d)) { fail_at(op); return mk_int(0); }
    return mk_flt(d);
}

// An operand MUST start with a digit. Anything else here is exactly one of the
// error cases: invalid character, a second operator in a row, or a missing operand
// at '\n' / end of file. All three are reported at p, the token we were looking
// at, so a single check covers them. At the last line without '\n', p sits on the
// '\0' sentinel = file length + 1, which is what the rules ask for.
// Floats are digits '.' digits. ".5" fails the digit check above (error at the
// '.'); "5." fails the check below (error at the '.'); "1.5e3" parses as 1.5 and
// the leftover 'e' is reported by process_file, which is the required position.
static Val parse_number(void) {
    skip_ws();
    if (!isdigit((unsigned char)*p)) { fail_at(p); return mk_int(0); }
    const char *s = p;
    while (isdigit((unsigned char)*p)) p++;
    if (*p != '.') {                          // plain integer
        long long v = 0;
        for (const char *c = s; c < p; c++) v = v * 10 + (*c - '0');
        return mk_int(v);
    }
    if (!isdigit((unsigned char)p[1])) { fail_at(p); return mk_int(0); }
    p++;
    while (isdigit((unsigned char)*p)) p++;
    // strtod would also swallow an exponent ("1.5e3") that we must reject, so we
    // hand it a private copy of exactly the digits '.' digits we validated.
    size_t n = (size_t)(p - s);
    char small[64];
    char *buf = n < sizeof small ? small : malloc(n + 1);
    if (!buf) { fprintf(stderr, "calc: out of memory\n"); exit(1); }
    memcpy(buf, s, n);
    buf[n] = '\0';
    double d = strtod(buf, NULL);
    if (buf != small) free(buf);
    return mk_flt(d);
}

static Val expr(void);                      // forward declaration: primary() and expr() call each other

// primary := NUMBER | '(' expr ')'
// A '(' restarts the whole grammar at the lowest level (expr), which is why
// (2 + 3) * 4 works: the parenthesised sum is finished before term() multiplies.
static Val primary(void) {
    skip_ws();
    if (*p == '(') {
        p++;
        Val v = expr();
        if (err_pos) return v;               // an error inside the parentheses wins
        skip_ws();
        // Missing ')' is reported where the line ends ('\n', or the '\0' sentinel
        // = file length + 1): that is the token we were looking at when we could
        // not continue. An EXTRA ')' is caught later in process_file, at that ')'.
        if (*p != ')') { fail_at(p); return mk_int(0); }
        p++;
        return v;
    }
    return parse_number();
}

// Integer power by repeated squaring: O(log e) multiplications, so even
// 1 ** 1000000000 is instant. x ** 0 is 1 for every x, including 0 ** 0 (rule 8).
static long long ipow(long long base, long long e) {
    long long r = 1;
    while (e > 0) {
        if (e & 1) r *= base;
        e >>= 1;
        if (e) base *= base;                 // skip the last squaring: it is never used and could overflow
    }
    return r;
}

// power := primary [ '**' power ]
// power() calls ITSELF for the exponent, so 2 ** 3 ** 2 groups as 2 ** (3 ** 2):
// the right-hand side swallows the rest of the chain first. That is what makes
// ** right-associative; a loop (as in expr/term) would group it from the left.
static Val power(void) {
    Val base = primary();
    if (err_pos) return base;
    skip_ws();
    if (p[0] == '*' && p[1] == '*') {        // '**' is one token; a lone '*' belongs to term()
        const char *op = p;                  // float overflow / NaN is reported at the operator
        p += 2;
        skip_ws();
        const char *exp_at = p;              // a negative integer exponent is reported at the START of the exponent
        Val e = power();
        if (err_pos) return base;            // a syntax error inside the exponent wins
        if (!base.is_float && !e.is_float) { // int ** int stays an integer; negative exponent has no integer result
            if (e.i < 0) { fail_at(exp_at); return mk_int(0); }
            return mk_int(ipow(base.i, e.i));
        }
        return check_float(pow(as_dbl(base), as_dbl(e)), op);
    }
    return base;
}

// term := power { ('*' | '/') power }
// expr below calls term() for its operands, so term binds tighter: in 3 + 2 * 5
// the 2 * 5 is finished inside term() before expr ever sees the '+'. That call
// structure IS the precedence; no table of priorities is needed.
static Val term(void) {
    Val v = power();
    for (;;) {
        if (err_pos) return v;
        skip_ws();
        if (*p == '*') {
            const char *op = p;
            p++;
            Val r = power();
            if (err_pos) return v;
            if (v.is_float || r.is_float) v = check_float(as_dbl(v) * as_dbl(r), op);
            else                          v = mk_int(v.i * r.i);
        } else if (*p == '/') {
            const char *op = p;
            p++;
            skip_ws();
            const char *rhs = p;             // division by zero is reported at the START of the right operand
            Val r = power();
            if (err_pos) return v;           // a syntax error came first, so it wins
            int zero = r.is_float ? (r.d == 0.0) : (r.i == 0);   // float 0.0 is a zero-division too (rule 9)
            if (zero) { fail_at(rhs); return v; }
            if (v.is_float || r.is_float) v = check_float(as_dbl(v) / as_dbl(r), op);
            else                          v = mk_int(v.i / r.i);  // C truncates toward zero, exactly what rule 8 asks for
        } else break;
    }
    return v;
}

// expr := term { ('+' | '-') term }
// The loop (not recursion) makes + and - LEFT-associative: 8 - 3 - 2 = (8-3)-2.
static Val expr(void) {
    Val v = term();
    for (;;) {
        if (err_pos) return v;               // stop at the first error
        skip_ws();
        if (*p == '+' || *p == '-') {
            const char *op = p;
            int plus = (*p == '+');
            p++;
            Val r = term();
            if (err_pos) return v;
            if (v.is_float || r.is_float) {
                double a = as_dbl(v), b = as_dbl(r);
                v = check_float(plus ? a + b : a - b, op);
            } else {
                v = mk_int(plus ? v.i + r.i : v.i - r.i);
            }
        } else break;
    }
    return v;
}
// ---- Files and folders --------------------------------------------------------
// "dir/task1.txt" -> "task1" (file name without folder and without .txt)
static void base_name(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    size_t n = strlen(name);
    if (n > 4 && !strcmp(name + n - 4, ".txt")) n -= 4;
    if (n >= cap) n = cap - 1;
    memcpy(out, name, n);
    out[n] = '\0';
}

// Evaluates one input file and writes <outdir>/<base>_<name>_<last>_<id>.txt
static int process_file(const char *input, const char *outdir) {
    size_t len;
    char *text = read_file(input, &len);
    if (!text) { fprintf(stderr, "calc: cannot read %s\n", input); return 1; }

    if (mkdir(outdir, 0777) != 0 && errno != EEXIST) {   // existing folder is fine
        fprintf(stderr, "calc: cannot create %s\n", outdir);
        free(text);
        return 1;
    }
    char base[256], outpath[8192];   // big enough for any outdir + file name, so snprintf never truncates
    base_name(input, base, sizeof base);
    snprintf(outpath, sizeof outpath, "%s/%s_%s_%s_%s.txt", outdir, base, NAME, LAST, ID);
    FILE *out = fopen(outpath, "w");
    if (!out) { fprintf(stderr, "calc: cannot write %s\n", outpath); free(text); return 1; }

       start = p = text;
    err_pos = 0;
    while (*p) {
        skip_ws();
        if (*p == '\n') { p++; continue; }   // blank line: no output
        if (!*p) break;
        // '#' is a comment only as the FIRST non-blank character of a line. We just
        // skip it like any other text; positions stay right because p never resets.
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        Val v = expr(); 
        skip_ws();
        // The expression ended but the line did not: whatever is left (e.g. '&')
        // is the token we could not continue with, so it is the error position.
        if (*p != '\n' && *p != '\0') fail_at(p);
        if (err_pos) break;
        if (v.is_float) fprintf(out, "%.15g\n", v.d);   // rule 10: floats with %.15g
        else            fprintf(out, "%lld\n", v.i);    // integers with %lld
        while (*p && *p != '\n') p++;        // move to the end of this line
    }
    if (err_pos) {
        // Rule 3: on any error the file holds ONLY the error line, so throw away
        // the results already written by reopening the file in "w" mode.
        FILE *again = freopen(outpath, "w", out);
        if (!again) { fprintf(stderr, "calc: cannot write %s\n", outpath); free(text); return 1; }
        out = again;
        fprintf(out, "ERROR:%zu\n", err_pos);
    }
    fclose(out);
    free(text);
    return 0;
}

// "labs/", "a/labs" or "labs" -> "labs": the folder's own name, used for the
// default output folder name when -d is given.
static void dir_base(const char *path, char *out, size_t cap) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;         // ignore trailing slashes
    size_t i = n;
    while (i > 0 && path[i - 1] != '/') i--;         // walk back to the last '/'
    size_t len = n - i;
    if (len >= cap) len = cap - 1;
    memcpy(out, path + i, len);
    out[len] = '\0';
}

// Evaluates every *.txt file directly inside dir. Subfolders and other file
// types are skipped (stat + S_ISREG also skips a *folder* that happens to be
// named something.txt). One bad file does not stop the others.
static int process_dir(const char *dir, const char *outdir) {
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "calc: cannot open folder %s\n", dir); return 1; }
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n <= 4 || strcmp(e->d_name + n - 4, ".txt") != 0) continue;
        char path[8192];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) {
            fprintf(stderr, "calc: path too long: %s\n", e->d_name);
            rc = 1;
            continue;
        }
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (process_file(path, outdir) != 0) rc = 1;
    }
    closedir(d);
    return rc;
}

static int usage(const char *why) {
    if (why) fprintf(stderr, "calc: %s\n", why);
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 1;                                        // any usage problem = non-zero exit (section 4)
}

int main(int argc, char **argv) {
    const char *outdir = NULL, *input = NULL, *dir = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-o") || !strcmp(a, "--output-dir")) {
            if (i + 1 >= argc) return usage("missing value after -o / --output-dir");
            outdir = argv[++i];
        } else if (!strcmp(a, "-d") || !strcmp(a, "--dir")) {
            if (i + 1 >= argc) return usage("missing value after -d / --dir");
            dir = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "calc: unknown option %s\n", a);
            return usage(NULL);
        } else if (input) {
            return usage("only one input file is allowed");
        } else {
            input = a;
        }
    }
    if (!dir && !input) return usage("no input file or folder given");
    if (dir && input)   return usage("use either an input file or -d, not both");

    char dflt[4096];                         // default folder: <input_base>_<name>_<id>
    if (!outdir) {
        char base[256];
        if (dir) dir_base(dir, base, sizeof base);
        else     base_name(input, base, sizeof base);
        snprintf(dflt, sizeof dflt, "%s_%s_%s", base, NAME, ID);
        outdir = dflt;
    }
    return dir ? process_dir(dir, outdir) : process_file(input, outdir);
}