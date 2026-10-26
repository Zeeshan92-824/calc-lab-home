// Zeeshan Kamal 251ADB053
// https://github.com/Zeeshan92-824/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used:  <every AI tool, website and book you used; details in comments where you used them>

// -std=c17 hides POSIX functions (mkdir, stat); this macro turns them back on.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <ctype.h>
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

static void skip_ws(void) {                 // spaces/tabs may sit between any tokens
    while (*p == ' ' || *p == '\t') p++;
}

static long long parse_number(void) {       // digits only for now (floats: Step 8)
    skip_ws();
    long long v = 0;
    while (isdigit((unsigned char)*p)) v = v * 10 + (*p++ - '0');
    return v;
}

// expr := number { ('+' | '-') number }
// The loop (not recursion) makes + and - LEFT-associative: 8 - 3 - 2 = (8-3)-2.
static long long expr(void) {
    long long v = parse_number();
    for (;;) {
        skip_ws();
        if (*p == '+')      { p++; v += parse_number(); }
        else if (*p == '-') { p++; v -= parse_number(); }
        else break;
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
    while (*p) {
        skip_ws();
        if (*p == '\n') { p++; continue; }   // blank line: no output
        if (!*p) break;
        fprintf(out, "%lld\n", expr());
        while (*p && *p != '\n') p++;        // move to the end of this line
    }
    fclose(out);
    free(text);
    return 0;
}

int main(int argc, char **argv) {
    const char *outdir = NULL, *input = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) outdir = argv[++i];
        else input = argv[i];
    }
    if (!input) { fprintf(stderr, "usage: calc [-o OUTDIR] input.txt\n"); return 1; }

    char dflt[4096];                         // default folder: <base>_<name>_<id>
    if (!outdir) {
        char base[256];
        base_name(input, base, sizeof base);
        snprintf(dflt, sizeof dflt, "%s_%s_%s", base, NAME, ID);
        outdir = dflt;
    }
    return process_file(input, outdir);
}