/* A portable, text-mode GW-BASIC-inspired interpreter. */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINES 10000
#define MAX_VARS 2048
#define MAX_STACK 256
#define TEXT 4096

typedef struct { double n; char s[TEXT]; int string; } Value;
typedef struct { int number; char *text; } Line;
typedef struct { char name[128]; Value value; } Variable;
typedef struct { int line; size_t offset; } Position;
typedef struct { int variable; double limit, step; Position start; } Loop;
static Line program[MAX_LINES];
static Variable variables[MAX_VARS];
static int line_count, variable_count, running, failed, quitting;
static int return_count, loop_count;
static Position pc, returns[MAX_STACK], data_pc;
static Loop loops[MAX_STACK];
static char *p;
static jmp_buf recovery;
static volatile sig_atomic_t interrupted;
static double last_random;
static int data_active, expression_depth;
typedef struct { char name[64]; int rank, bounds[4]; } Array;
static Array arrays[128];
static int array_count;

static void error(const char *message) {
    fprintf(stderr, "?%s", message);
    if (running && pc.line < line_count) fprintf(stderr, " in %d", program[pc.line].number);
    fputc('\n', stderr);
    failed = 1;
    longjmp(recovery, 1);
}
static void *allocate(size_t size) {
    void *memory = malloc(size);
    if (!memory) error("Out of memory");
    return memory;
}
static char *copy(const char *s) {
    char *result = allocate(strlen(s) + 1);
    strcpy(result, s);
    return result;
}
static void spaces(void) { while (isspace((unsigned char)*p)) p++; }
static int name_char(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$' || c == '%'; }
static int keyword(const char *word) {
    spaces();
    size_t n = strlen(word);
    for (size_t i = 0; i < n; i++)
        if (!p[i] || toupper((unsigned char)p[i]) != word[i]) return 0;
    if (name_char(p[n])) return 0;
    p += n;
    return 1;
}
static int symbol(char c) { spaces(); if (*p == c) { p++; return 1; } return 0; }
static void expect(char c) { if (!symbol(c)) error("Syntax error"); }
static void identifier(char *name) {
    spaces();
    if (!isalpha((unsigned char)*p)) error("Expected variable");
    size_t n = 0;
    while (name_char(*p)) {
        if (n >= 63) error("Variable name too long");
        name[n++] = (char)toupper((unsigned char)*p++);
    }
    name[n] = 0;
}
static Value number(double n) { Value v = {0}; v.n = n; return v; }
static Value string(const char *s) {
    Value v = {0}; v.string = 1;
    if (strlen(s) >= TEXT) error("String too long");
    strcpy(v.s, s); return v;
}
static double numeric(Value v) { if (v.string) error("Type mismatch"); return v.n; }
static int integer(double n) {
    if (!isfinite(n) || n < INT_MIN || n > INT_MAX) error("Overflow");
    return (int)n;
}
static int variable(const char *name) {
    for (int i = 0; i < variable_count; i++) if (!strcmp(name, variables[i].name)) return i;
    if (variable_count == MAX_VARS) error("Too many variables");
    int i = variable_count++;
    strcpy(variables[i].name, name);
    variables[i].value = strchr(name, '$') ? string("") : number(0);
    return i;
}
static void assign(int index, Value value) {
    if (variables[index].value.string != value.string) error("Type mismatch");
    if (strchr(variables[index].name, '%')) value.n = integer(value.n);
    variables[index].value = value;
}
static Value expression(int minimum);
/* Array elements share the variable store, using an internal indexed key. */
static int array_reference(const char *name) {
    int indices[4], rank = 0;
    do {
        if (rank == 4) error("Too many subscripts");
        indices[rank++] = integer(numeric(expression(0)));
    } while (symbol(','));
    expect(')');
    int a = 0;
    while (a < array_count && strcmp(arrays[a].name, name)) a++;
    if (a == array_count) {
        if (array_count == 128) error("Too many arrays");
        strcpy(arrays[a].name, name); arrays[a].rank = rank;
        for (int i = 0; i < rank; i++) arrays[a].bounds[i] = 10;
        array_count++;
    }
    if (arrays[a].rank != rank) error("Wrong number of subscripts");
    char key[128]; strcpy(key, name); strcat(key, "(");
    for (int i = 0; i < rank; i++) {
        if (indices[i] < 0 || indices[i] > arrays[a].bounds[i]) error("Subscript out of range");
        char index[24]; snprintf(index, sizeof(index), "%d,", indices[i]); strcat(key, index);
    }
    return variable(key);
}
static int reference(void) {
    char name[64]; identifier(name);
    return symbol('(') ? array_reference(name) : variable(name);
}
static int is_function(const char *name) {
    static const char *names[] = {"LEFT$", "RIGHT$", "MID$", "LEN", "VAL", "ASC",
        "STR$", "CHR$", "ABS", "INT", "FIX", "SGN", "SQR", "SIN", "COS", "TAN",
        "ATN", "LOG", "EXP", "RND"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) if (!strcmp(name, names[i])) return 1;
    return 0;
}
static Value function(const char *name) {
    Value a = expression(0), b = number(0), c = number(0);
    int count = 1;
    if (symbol(',')) { b = expression(0); count++; }
    if (symbol(',')) { c = expression(0); count++; }
    expect(')');
    if (!strcmp(name, "LEFT$") || !strcmp(name, "RIGHT$") || !strcmp(name, "MID$")) {
        if (!a.string || count < 2) error("Illegal function call");
        int start = 0, length = integer(numeric(b)), size = (int)strlen(a.s);
        if (!strcmp(name, "MID$")) { start = length - 1; length = count == 3 ? integer(numeric(c)) : size; }
        else if (!strcmp(name, "RIGHT$")) start = size - length;
        if (length < 0 || (!strcmp(name, "MID$") && start < 0)) error("Illegal function call");
        if (start < 0) start = 0;
        if (start > size) start = size;
        if (length > size - start) length = size - start;
        memmove(a.s, a.s + start, (size_t)length); a.s[length] = 0; return a;
    }
    if (count != 1) error("Illegal function call");
    if (!strcmp(name, "LEN")) { if (!a.string) error("Type mismatch"); return number((double)strlen(a.s)); }
    if (!strcmp(name, "VAL")) { if (!a.string) error("Type mismatch"); return number(strtod(a.s, NULL)); }
    if (!strcmp(name, "ASC")) { if (!a.string || !*a.s) error("Illegal function call"); return number((unsigned char)a.s[0]); }
    double x = numeric(a), result;
    if (!strcmp(name, "STR$")) { char s[64]; snprintf(s, sizeof(s), "%.12g", x); return string(s); }
    if (!strcmp(name, "CHR$")) { int n = integer(x); if (n < 1 || n > 255) error("Illegal function call"); char s[2] = {(char)n, 0}; return string(s); }
    if (!strcmp(name, "ABS")) result = fabs(x);
    else if (!strcmp(name, "INT")) result = floor(x);
    else if (!strcmp(name, "FIX")) result = trunc(x);
    else if (!strcmp(name, "SGN")) result = (x > 0) - (x < 0);
    else if (!strcmp(name, "SQR")) result = sqrt(x);
    else if (!strcmp(name, "SIN")) result = sin(x);
    else if (!strcmp(name, "COS")) result = cos(x);
    else if (!strcmp(name, "TAN")) result = tan(x);
    else if (!strcmp(name, "ATN")) result = atan(x);
    else if (!strcmp(name, "LOG")) result = log(x);
    else if (!strcmp(name, "EXP")) result = exp(x);
    else if (!strcmp(name, "RND")) {
        if (x < 0) srand((unsigned)integer(-x));
        if (x != 0) last_random = (double)rand() / ((double)RAND_MAX + 1);
        result = last_random;
    } else { error("Unknown function"); result = 0; }
    if (!isfinite(result)) error("Illegal function call");
    return number(result);
}
static Value primary(void) {
    spaces();
    if (symbol('(')) { Value v = expression(0); expect(')'); return v; }
    if (symbol('+')) return number(numeric(expression(7)));
    if (symbol('-')) return number(-numeric(expression(7)));
    if (keyword("NOT")) return number(~integer(numeric(expression(3))));
    if (symbol('"')) {
        char s[TEXT]; size_t n = 0;
        while (*p && *p != '"') { if (n == TEXT - 1) error("String too long"); s[n++] = *p++; }
        if (*p != '"') error("Unterminated string");
        p++; s[n] = 0; return string(s);
    }
    if (isdigit((unsigned char)*p) || *p == '.') {
        char *end; double n = strtod(p, &end);
        if (end == p || !isfinite(n)) error("Invalid number");
        p = end; return number(n);
    }
    char name[64]; identifier(name);
    if (symbol('(')) return is_function(name) ? function(name) : variables[array_reference(name)].value;
    if (!strcmp(name, "RND")) { last_random = (double)rand() / ((double)RAND_MAX + 1); return number(last_random); }
    return variables[variable(name)].value;
}
/* Precedence increases from OR through exponentiation. */
static Value expression(int minimum) {
    if (++expression_depth > 64) error("Expression too complex");
    Value a = primary();
    for (;;) {
        spaces(); char *saved = p; int op = 0, precedence = 0;
        if (keyword("OR")) { op = 'o'; precedence = 1; }
        else if (keyword("AND")) { op = 'a'; precedence = 2; }
        else if (keyword("MOD")) { op = 'm'; precedence = 5; }
        else if (*p == '=' || *p == '<' || *p == '>') {
            op = *p++; precedence = 3;
            if (op == '<' && *p == '>') { op = '!'; p++; }
            else if (*p == '=') { op = op == '<' ? 'l' : 'g'; p++; }
        } else if (*p == '+' || *p == '-') { op = *p++; precedence = 4; }
        else if (*p == '*' || *p == '/' || *p == '\\') { op = *p++; precedence = 6; }
        else if (*p == '^') { op = *p++; precedence = 8; }
        if (!op || precedence < minimum) { p = saved; break; }
        Value b = expression(precedence + (op != '^'));
        if (a.string || b.string) {
            if (!a.string || !b.string) error("Type mismatch");
            if (op == '+') { if (strlen(a.s) + strlen(b.s) >= TEXT) error("String too long"); strcat(a.s, b.s); continue; }
            if (precedence != 3) error("Type mismatch");
            int cmp = strcmp(a.s, b.s);
            a = number(-(op == '=' ? cmp == 0 : op == '!' ? cmp != 0 : op == '<' ? cmp < 0 : op == '>' ? cmp > 0 : op == 'l' ? cmp <= 0 : cmp >= 0));
            continue;
        }
        double x = a.n, y = b.n;
        switch (op) {
        case '+': a.n = x + y; break; case '-': a.n = x - y; break;
        case '*': a.n = x * y; break;
        case '/': if (!y) error("Division by zero"); a.n = x / y; break;
        case '\\': case 'm': { int ix = integer(x), iy = integer(y); if (!iy) error("Division by zero"); if (ix == INT_MIN && iy == -1) error("Overflow"); a.n = op == 'm' ? ix % iy : ix / iy; break; }
        case '^': a.n = pow(x, y); break;
        case '=': a.n = -(x == y); break; case '!': a.n = -(x != y); break;
        case '<': a.n = -(x < y); break; case '>': a.n = -(x > y); break;
        case 'l': a.n = -(x <= y); break; case 'g': a.n = -(x >= y); break;
        case 'a': a.n = integer(x) & integer(y); break;
        case 'o': a.n = integer(x) | integer(y); break;
        }
        if (!isfinite(a.n)) error("Overflow");
    }
    expression_depth--;
    return a;
}
static int find_line(int number_) {
    for (int i = 0; i < line_count; i++) if (program[i].number == number_) return i;
    error("Undefined line number"); return 0;
}
static void store_line(int n, const char *text) {
    if (n < 1 || n > 65529) error("Illegal line number");
    data_pc = (Position){0, 0}; data_active = 0; return_count = loop_count = 0;
    int i = 0;
    while (i < line_count && program[i].number < n) i++;
    if (i < line_count && program[i].number == n) {
        free(program[i].text);
        memmove(program + i, program + i + 1, (size_t)(--line_count - i) * sizeof(Line));
    }
    if (!*text) return;
    if (line_count == MAX_LINES) error("Too many lines");
    memmove(program + i + 1, program + i, (size_t)(line_count++ - i) * sizeof(Line));
    program[i] = (Line){n, copy(text)};
}
static void clear_program(void) { for (int i = 0; i < line_count; i++) free(program[i].text); line_count = 0; }
static void reset(void) { variable_count = return_count = loop_count = array_count = 0; data_pc = (Position){0, 0}; data_active = 0; }
static int end_statement(void) {
    spaces(); char *saved = p;
    int result = !*p || *p == ':' || *p == '\'' || keyword("ELSE");
    p = saved; return result;
}
static Position following(void) {
    spaces();
    if (*p == ':') return (Position){pc.line, (size_t)(p + 1 - program[pc.line].text)};
    if (keyword("ELSE")) return (Position){pc.line + 1, 0};
    if (*p && *p != '\'') error("Syntax error");
    return (Position){pc.line + 1, 0};
}
static void require_running(void) { if (!running) error("Illegal direct"); }
static int line_number(void) { return integer(numeric(expression(0))); }
static void filename(char *out) { Value v = expression(0); if (!v.string) error("Expected filename string"); strcpy(out, v.s); if (!end_statement()) error("Syntax error"); }
static void load(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) error("Cannot open file");
    /* Validate before replacing the current program. */
    Line *loaded = calloc(MAX_LINES, sizeof(Line));
    if (!loaded) { fclose(file); error("Out of memory"); }
    char buffer[TEXT]; int count = 0, bad = 0;
    while (fgets(buffer, sizeof(buffer), file)) {
        size_t len = strlen(buffer);
        if (len == TEXT - 1 && buffer[len - 1] != '\n') { bad = 1; break; }
        char *s = buffer; while (isspace((unsigned char)*s)) s++;
        if (!*s) continue;
        char *end; long n = strtol(s, &end, 10);
        if (end == s || n < 1 || n > 65529 || count == MAX_LINES) { bad = 1; break; }
        while (isspace((unsigned char)*end)) end++;
        end[strcspn(end, "\r\n")] = 0;
        loaded[count++] = (Line){(int)n, copy(end)};
    }
    if (ferror(file)) bad = 1;
    fclose(file);
    if (!bad) {
        clear_program(); reset();
        for (int i = 0; i < count; i++) store_line(loaded[i].number, loaded[i].text);
    }
    for (int i = 0; i < count; i++) free(loaded[i].text);
    free(loaded);
    if (bad) error("Invalid BASIC file");
}
static Value read_data(void) {
    char *saved = p;
    while (data_pc.line < line_count) {
        char *base = program[data_pc.line].text;
        p = base + data_pc.offset;
        if (!data_active) {
            int found = 0, quoted = 0;
            for (;;) {
                if (keyword("DATA")) { found = 1; break; }
                if (*p == '\'' || keyword("REM")) break;
                while (*p) { if (*p == '"') quoted = !quoted; if (!quoted && *p == ':') { p++; break; } p++; }
                if (!*p) break;
            }
            if (!found) { data_pc.line++; data_pc.offset = 0; continue; }
            data_active = 1;
        }
        spaces();
        Value v;
        if (*p == '"') v = primary();
        else {
            char item[TEXT]; size_t n = 0;
            while (*p && *p != ',' && *p != ':') item[n++] = *p++;
            while (n && isspace((unsigned char)item[n - 1])) n--;
            item[n] = 0; char *end; double x = strtod(item, &end);
            v = *item && !*end ? number(x) : string(item);
        }
        spaces();
        if (*p == ',') data_pc.offset = (size_t)(++p - base);
        else {
            data_active = 0;
            if (*p == ':') data_pc.offset = (size_t)(p + 1 - base);
            else { data_pc.line++; data_pc.offset = 0; }
        }
        p = saved; return v;
    }
    p = saved; error("Out of DATA"); return number(0);
}
/* Returns true when execution has explicitly changed the program counter. */
static int statement(void) {
    spaces();
    if (!*p) return 0;
    if (*p == '\'' || keyword("REM")) { p += strlen(p); return 0; }
    if (keyword("PRINT") || symbol('?')) {
        int newline = 1;
        while (!end_statement()) {
            if (symbol(';')) { newline = 0; continue; }
            if (symbol(',')) { putchar('\t'); newline = 0; continue; }
            Value v = expression(0);
            if (v.string) fputs(v.s, stdout); else printf("%.12g", v.n);
            newline = 1;
            spaces(); if (*p != ';' && *p != ',' && !end_statement()) error("Syntax error");
        }
        if (newline) putchar('\n');
    } else if (keyword("IF")) {
        double condition = numeric(expression(0));
        if (!keyword("THEN")) error("Expected THEN");
        if (!condition) {
            int quoted = 0;
            while (*p) {
                if (*p == '"') quoted = !quoted;
                if (!quoted && !name_char(p[-1]) && toupper((unsigned char)*p) == 'E' && keyword("ELSE")) break;
                if (*p) p++;
            }
        }
        spaces();
        if (isdigit((unsigned char)*p)) { require_running(); int target = line_number(); pc = (Position){find_line(target), 0}; return 1; }
        if (*p) return statement();
    } else if (keyword("ELSE")) { p += strlen(p); }
    else if (keyword("GOTO")) { require_running(); int target = line_number(); pc = (Position){find_line(target), 0}; return 1; }
    else if (keyword("GOSUB")) {
        require_running(); int target = line_number();
        if (return_count == MAX_STACK) error("GOSUB stack overflow");
        returns[return_count++] = following(); pc = (Position){find_line(target), 0}; return 1;
    } else if (keyword("RETURN")) {
        require_running(); if (!return_count) error("RETURN without GOSUB"); pc = returns[--return_count]; return 1;
    } else if (keyword("FOR")) {
        require_running(); char name[64]; identifier(name); int index = variable(name);
        expect('='); Value start = expression(0); assign(index, start); numeric(start);
        if (!keyword("TO")) error("Expected TO");
        double limit = numeric(expression(0)), step = 1;
        if (keyword("STEP")) step = numeric(expression(0));
        if (!step) error("Illegal STEP");
        if (loop_count == MAX_STACK) error("FOR stack overflow");
        Position next = following();
        loops[loop_count++] = (Loop){index, limit, step, next};
        if ((step > 0 && start.n > limit) || (step < 0 && start.n < limit)) {
            int depth = 1; Position scan = next;
            while (scan.line < line_count) {
                p = program[scan.line].text + scan.offset; spaces();
                if (keyword("FOR")) depth++;
                else if (keyword("NEXT") && --depth == 0) {
                    while (*p && *p != ':') p++;
                    pc = *p == ':' ? (Position){scan.line, (size_t)(p + 1 - program[scan.line].text)} : (Position){scan.line + 1, 0};
                    loop_count--; return 1;
                }
                int quoted = 0;
                while (*p) { if (*p == '"') quoted = !quoted; if (!quoted && *p == ':') break; p++; }
                if (*p == ':') scan.offset = (size_t)(p + 1 - program[scan.line].text);
                else { scan.line++; scan.offset = 0; }
            }
            error("FOR without NEXT");
        }
    } else if (keyword("NEXT")) {
        require_running(); if (!loop_count) error("NEXT without FOR");
        Loop *loop = &loops[loop_count - 1];
        if (!end_statement()) { char name[64]; identifier(name); if (variable(name) != loop->variable) error("Mismatched NEXT"); }
        Value v = variables[loop->variable].value; v.n += loop->step; assign(loop->variable, v);
        if (loop->step > 0 ? v.n <= loop->limit : v.n >= loop->limit) { pc = loop->start; return 1; }
        loop_count--;
    } else if (keyword("INPUT")) {
        spaces();
        if (*p == '"') { Value prompt = primary(); fputs(prompt.s, stdout); if (!symbol(';') && !symbol(',')) error("Syntax error"); }
        do {
            char buffer[TEXT]; int index = reference();
            fputs("? ", stdout); fflush(stdout);
            if (!fgets(buffer, sizeof(buffer), stdin)) error("Input past end");
            buffer[strcspn(buffer, "\r\n")] = 0;
            if (variables[index].value.string) assign(index, string(buffer));
            else { char *end; double n = strtod(buffer, &end); if (end == buffer) error("Invalid input"); while (isspace((unsigned char)*end)) end++; if (*end || !isfinite(n)) error("Invalid input"); assign(index, number(n)); }
        } while (symbol(','));
    } else if (keyword("DIM")) {
        do {
            char name[64]; identifier(name);
            for (int i = 0; i < array_count; i++) if (!strcmp(arrays[i].name, name)) error("Array already dimensioned");
            if (array_count == 128) error("Too many arrays");
            Array a = {0}; strcpy(a.name, name); expect('(');
            do {
                if (a.rank == 4) error("Too many subscripts");
                int bound = integer(numeric(expression(0)));
                if (bound < 0 || bound > 32767) error("Illegal dimension");
                a.bounds[a.rank++] = bound;
            } while (symbol(','));
            expect(')'); arrays[array_count++] = a;
        } while (symbol(','));
    } else if (keyword("DATA")) { int quoted = 0; while (*p) { if (*p == '"') quoted = !quoted; if (*p == ':' && !quoted) break; p++; } }
    else if (keyword("READ")) { do { int index = reference(); assign(index, read_data()); } while (symbol(',')); }
    else if (keyword("RESTORE")) { data_pc = (Position){end_statement() ? 0 : find_line(line_number()), 0}; data_active = 0; }
    else if (keyword("END") || keyword("STOP")) { running = 0; p += strlen(p); return 1; }
    else if (keyword("RANDOMIZE")) { unsigned seed = end_statement() ? (unsigned)time(NULL) : (unsigned)integer(numeric(expression(0))); srand(seed); }
    else if (keyword("CLS")) { fputs("\033[2J\033[H", stdout); }
    else if (keyword("CLEAR")) { reset(); }
    else if (keyword("RUN")) {
        int start = end_statement() ? 0 : find_line(line_number()); reset(); running = 1; pc = (Position){start, 0}; return 1;
    } else if (keyword("LIST")) {
        int low = 0, high = 65529; spaces();
        if (isdigit((unsigned char)*p)) { low = (int)strtol(p, &p, 10); high = low; }
        if (symbol('-')) { spaces(); high = isdigit((unsigned char)*p) ? (int)strtol(p, &p, 10) : 65529; }
        for (int i = 0; i < line_count; i++) if (program[i].number >= low && program[i].number <= high) printf("%d %s\n", program[i].number, program[i].text);
    } else if (keyword("NEW")) {
        if (running) error("Illegal in program");
        clear_program(); reset();
    } else if (keyword("SAVE")) {
        char path[TEXT]; filename(path); FILE *file = fopen(path, "w"); if (!file) error("Cannot create file");
        int bad = 0; for (int i = 0; i < line_count; i++) if (fprintf(file, "%d %s\n", program[i].number, program[i].text) < 0) bad = 1;
        if (fclose(file)) bad = 1;
        if (bad) error("File write error");
    } else if (keyword("LOAD")) {
        if (running) error("Illegal in program");
        char path[TEXT]; filename(path); load(path);
    } else if (keyword("SYSTEM") || keyword("QUIT") || keyword("EXIT")) { quitting = 1; running = 0; p += strlen(p); }
    else {
        keyword("LET"); int index = reference(); expect('='); assign(index, expression(0));
    }
    return 0;
}
static void execute(char *text) {
    p = text;
    for (;;) {
        if (interrupted) { interrupted = 0; error("Break"); }
        int jumped = statement();
        if (quitting) break;
        if (running) {
            if (!jumped) {
                spaces();
                if (keyword("ELSE")) p += strlen(p);
                pc = following();
            }
            if (pc.line >= line_count) { running = 0; break; }
            p = program[pc.line].text + pc.offset;
        } else {
            if (jumped) break;
            if (symbol(':')) continue;
            if (keyword("ELSE")) p += strlen(p);
            spaces(); if (*p && *p != '\'') error("Syntax error");
            break;
        }
    }
}
static void on_interrupt(int signal_) { (void)signal_; interrupted = 1; }
int main(int argc, char **argv) {
    static int quiet; static const char *path;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quiet") || !strcmp(argv[i], "-q")) quiet = 1;
        else if (!strcmp(argv[i], "--help")) { puts("Usage: gkbasic [-q|--quiet] [program.bas]\nWith a file, run it and exit. Otherwise start the BASIC prompt."); return 0; }
        else if (!path && argv[i][0] != '-') path = argv[i];
        else { fputs("Invalid arguments; use --help\n", stderr); return 2; }
    }
    signal(SIGINT, on_interrupt); srand((unsigned)time(NULL));
    if (path) {
        if (!setjmp(recovery)) { load(path); char command[] = "RUN"; execute(command); }
        clear_program(); return failed ? 1 : 0;
    }
    if (!quiet) puts("GK-BASIC 1.0\nGW-BASIC-style C interpreter. Type SYSTEM to exit.");
    char buffer[TEXT];
    for (;;) {
        if (setjmp(recovery)) { running = 0; expression_depth = 0; }
        if (quitting) break;
        if (!quiet) { fputs("Ok\n", stdout); fflush(stdout); }
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        size_t n = strlen(buffer);
        if (n == sizeof(buffer) - 1 && buffer[n - 1] != '\n') {
            int ch; while ((ch = getchar()) != '\n' && ch != EOF) {} error("Line too long");
        }
        buffer[strcspn(buffer, "\r\n")] = 0;
        p = buffer; spaces();
        if (isdigit((unsigned char)*p)) {
            char *end; errno = 0; long line = strtol(p, &end, 10);
            if (errno || line < 1 || line > 65529) error("Illegal line number");
            p = end; spaces(); store_line((int)line, p);
        } else execute(p);
    }
    clear_program(); return failed ? 1 : 0;
}
