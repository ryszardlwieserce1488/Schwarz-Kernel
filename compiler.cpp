// ten plik jest używany przez jądro 64-bitowego systemu operacyjnego w celu kompilacji kodu C,
// docelowo będzie kompilować całość języka C. Obecnie kompiluje zaledwie część C i umożliwia
// uruchomienie tak skompilowanego kodu.
// nie mamy dostępu do ŻADNYCH bibliotek poza naszymi własnymi.

/*
    compiler.cpp
    ============

    Ten plik implementuje mały kompilator/JIT działający wewnątrz jądra systemu. Nie jest to
    jeszcze pełny kompilator ISO C, tylko własny podzbiór języka C zaprojektowany tak, aby dało
    się go uruchomić w środowisku freestanding, bez libc i bez zewnętrznego runtime.

    Najważniejsze fakty:
    - wejściem jest pojedynczy tekst źródłowy;
    - lexer zamienia go na tokeny;
    - parser rekurencyjny od razu emituje kod x86-64 do `code_buffer`;
    - `execute_compiled()` traktuje bufor jako funkcję `int fn(void)` i ją uruchamia.

    Status zgodności:
    - to NIE jest pełna implementacja standardu C;
    - poprawniej traktować ten plik jako kompilator małego, własnego dialektu C;
    - część zachowań jest zgodna lub zbliżona do C, ale wiele elementów standardu nadal nie
      istnieje albo jest uproszczonych.

    Co jest obecnie obsługiwane:
    - typy skalarne: `int`, `char`, `int8_t`, `int16_t`, `int32_t`, `int64_t`,
      `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `intptr_t`, `uintptr_t`;
    - wskaźniki do powyższych typów;
    - lokalne tablice o stałym rozmiarze: `TYPE name[N]`;
    - liczby dziesiętne i szesnastkowe;
    - proste stałe znakowe, np. `'A'`, `'\n'`, `'\0'`;
    - operatory: `+ - * / %`, porównania, `&&`, `||`, `!`, `&`, `*`, `=`, `+=`, `-=`,
      `*=`, `/=`, `%=`, pre/post `++`, pre/post `--`;
    - instrukcje: deklaracje, przypisania, `if`, `else`, `while`, `do ... while`,
      `for`, `return`;
    - indeksowanie tablic i wskaźników: `a[i]`.

    Najważniejsze ograniczenia względem standardu C:
    - brak funkcji użytkownika i wywołań funkcji;
    - brak scope blokowego: wszystkie zmienne żyją w jednej tabeli symboli aż do końca źródła;
    - brak preprocesora, `sizeof`, struktur, unii, enumów, string literal, initializer list,
      `switch`, `break`, `continue`, operatora przecinka i wielu innych elementów C;
    - brak pełnych "usual arithmetic conversions";
    - brak pełnego modelu lvalue/rvalue; część przypisań jest rozpoznawana specjalnie w
      `statement()`, a nie przez ogólny system lvalue;
    - tablice są tylko lokalne, jednowymiarowe i o stałym rozmiarze podanym literałem.

    Decyzje implementacyjne:
    - zwykły `char` jest tutaj traktowany jako signed 8-bit;
    - nazwa tablicy w wyrażeniu degeneruje do wskaźnika na pierwszy element;
    - `&array` jest upraszczane do adresu pierwszego elementu;
    - parser korzysta głównie z jednego tokena lookahead, więc nowe konstrukcje najlepiej
      dodawać małymi, lokalnymi krokami.

    Mapa pliku:
    1. Błędy: magazyn diagnostyki kompilacji.
    2. System typów: opis typów bazowych, wskaźników i tablic.
    3. Lexer: tokenizacja źródła.
    4. Tabela zmiennych: pojedyncza ramka stosu i metadane lokalnych symboli.
    5. Emitter: surowa emisja x86-64.
    6. Parser + codegen: analiza składni i generacja kodu bez pośredniego IR.
    7. Execute: uruchomienie wygenerowanego bufora jako kodu.

    Jeśli rozwijasz ten kompilator dalej, zwykle trzeba aktualizować kilka miejsc naraz:
    - nowy typ: `BaseType`, tokeny, lexer słów kluczowych, rozmiar, signedness, load/store;
    - nowy operator: lexer, precedencję parsera i odpowiednią emisję;
    - nowe lvalue: parser instrukcji oraz helpery obliczania adresu/wartości;
    - nowe reguły semantyczne: zarówno diagnostykę, jak i generację kodu.
*/

#include "types.h"   // uint64_t, uint32_t, uint16_t, uint8_t, int64_t, int32_t, int16_t, int8_t, uintptr_t, intptr_t
#include "memory.h"  // memory_init, malloc, free, memory_free_bytes, memory_used_bytes
#include "compiler.h"// lex, parse_and_compile, execute_compiled, compile_error_count, compile_get_error

// ─────────────────────────────────────────
//  BŁĘDY
// ─────────────────────────────────────────

struct CompileError {
    int  line;
    char msg[128];
};

static CompileError error_list[64];
static int          error_count = 0;
static bool         explicit_return_seen = false;

static void my_strcpy(char* dst, const char* src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void my_strcat(char* dst, const char* src, int max) {
    int i = 0; while (dst[i]) i++;
    int j = 0;
    while (i < max - 1 && src[j]) { dst[i++] = src[j++]; }
    dst[i] = '\0';
}

static int my_strcmp(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return *a - *b; a++; b++; }
    return *a - *b;
}

static void add_error(int line, const char* msg) {
    if (error_count >= 64) return;
    error_list[error_count].line = line;
    my_strcpy(error_list[error_count].msg, msg, 128);
    error_count++;
}

static void add_error_tok(int line, const char* msg, const char* detail) {
    if (error_count >= 64) return;
    error_list[error_count].line = line;
    my_strcpy(error_list[error_count].msg, msg, 128);
    my_strcat(error_list[error_count].msg, detail, 128);
    error_count++;
}

int  compile_error_count() { return error_count; }
bool compile_has_explicit_return() { return explicit_return_seen; }

void compile_get_error(int idx, int* out_line, const char** out_msg) {
    if (idx < 0 || idx >= error_count) { *out_line = 0; *out_msg = ""; return; }
    *out_line = error_list[idx].line;
    *out_msg = error_list[idx].msg;
}

static void errors_reset() { error_count = 0; }

// ─────────────────────────────────────────
//  SYSTEM TYPÓW
// ─────────────────────────────────────────

enum BaseType {
    BT_CHAR,
    BT_INT,
    BT_INT8, BT_INT16, BT_INT32, BT_INT64,
    BT_UINT8, BT_UINT16, BT_UINT32, BT_UINT64,
    BT_UINTPTR, BT_INTPTR,
    BT_UNKNOWN_T
};

struct VarType {
    BaseType base;
    bool     is_ptr;
    int      array_len;
};

static VarType make_type(BaseType b, bool ptr, int array_len = 0) { VarType t; t.base = b; t.is_ptr = ptr; t.array_len = array_len; return t; }
static VarType type_int() { return make_type(BT_INT, false, 0); }

static int basetype_size(BaseType b) {
    switch (b) {
    case BT_CHAR:
    case BT_INT8:  case BT_UINT8:  return 1;
    case BT_INT16: case BT_UINT16: return 2;
    case BT_INT:
    case BT_INT32: case BT_UINT32: return 4;
    default: return 8;
    }
}

static bool basetype_is_signed(BaseType b) {
    return b == BT_CHAR || b == BT_INT || b == BT_INT8 || b == BT_INT16 ||
        b == BT_INT32 || b == BT_INT64 || b == BT_INTPTR;
}

static int pointee_size(VarType t) {
    if (!t.is_ptr) return 0;
    return basetype_size(t.base);
}

// ─────────────────────────────────────────
//  LEXER
// ─────────────────────────────────────────

enum TokenType {
    T_CHAR,
    T_INT,
    T_INT8, T_INT16, T_INT32, T_INT64,
    T_UINT8, T_UINT16, T_UINT32, T_UINT64,
    T_UINTPTR, T_INTPTR,
    T_RETURN, T_IF, T_ELSE,
    T_WHILE, T_DO, T_FOR,          // ← nowe: pętle
    T_IDENT, T_NUMBER,
    T_ASSIGN,
    T_PLUSEQ, T_MINUSEQ,           // ← nowe: +=, -=
    T_STAREQ, T_SLASHEQ,           // ← nowe: *=, /=
    T_PERCENTEQ,                   // ← nowe: %=
    T_EQ, T_NEQ, T_LT, T_GT, T_LEQ, T_GEQ,
    T_PLUS, T_MINUS, T_STAR, T_SLASH,
    T_PERCENT,                     // ← nowe: %
    T_AMPERSAND,
    T_AND,                         // ← nowe: &&
    T_PIPE,                        // ← nowe: |  (bitowy, pomocniczy)
    T_OR,                          // ← nowe: ||
    T_NOT,                         // ← nowe: !
    T_PLUSPLUS,                    // ← nowe: ++
    T_MINUSMINUS,                  // ← nowe: --
    T_SEMICOLON,
    T_LPAREN, T_RPAREN,
    T_LBRACKET, T_RBRACKET,
    T_LBRACE, T_RBRACE,
    T_EOF, T_UNKNOWN
};

static const char* token_name(TokenType t) {
    switch (t) {
    case T_CHAR:      return "char";
    case T_INT:       return "int";
    case T_INT8:      return "int8_t";
    case T_INT16:     return "int16_t";
    case T_INT32:     return "int32_t";
    case T_INT64:     return "int64_t";
    case T_UINT8:     return "uint8_t";
    case T_UINT16:    return "uint16_t";
    case T_UINT32:    return "uint32_t";
    case T_UINT64:    return "uint64_t";
    case T_UINTPTR:   return "uintptr_t";
    case T_INTPTR:    return "intptr_t";
    case T_RETURN:    return "return";
    case T_IF:        return "if";
    case T_ELSE:      return "else";
    case T_WHILE:     return "while";
    case T_DO:        return "do";
    case T_FOR:       return "for";
    case T_IDENT:     return "identyfikator";
    case T_NUMBER:    return "liczba";
    case T_ASSIGN:    return "'='";
    case T_PLUSEQ:    return "'+='";
    case T_MINUSEQ:   return "'-='";
    case T_STAREQ:    return "'*='";
    case T_SLASHEQ:   return "'/='";
    case T_PERCENTEQ: return "'%='";
    case T_EQ:        return "'=='";
    case T_NEQ:       return "'!='";
    case T_LT:        return "'<'";
    case T_GT:        return "'>'";
    case T_LEQ:       return "'<='";
    case T_GEQ:       return "'>='";
    case T_PLUS:      return "'+'";
    case T_MINUS:     return "'-'";
    case T_STAR:      return "'*'";
    case T_SLASH:     return "'/'";
    case T_PERCENT:   return "'%'";
    case T_AMPERSAND: return "'&'";
    case T_AND:       return "'&&'";
    case T_PIPE:      return "'|'";
    case T_OR:        return "'||'";
    case T_NOT:       return "'!'";
    case T_PLUSPLUS:  return "'++'";
    case T_MINUSMINUS:return "'--'";
    case T_SEMICOLON: return "';'";
    case T_LPAREN:    return "'('";
    case T_RPAREN:    return "')'";
    case T_LBRACKET:  return "'['";
    case T_RBRACKET:  return "']'";
    case T_LBRACE:    return "'{'";
    case T_RBRACE:    return "'}'";
    case T_EOF:       return "koniec pliku";
    default:          return "nieznany";
    }
}

static bool is_type_token(TokenType t) {
    return t == T_CHAR || t == T_INT || t == T_INT8 || t == T_INT16 || t == T_INT32 ||
        t == T_INT64 || t == T_UINT8 || t == T_UINT16 || t == T_UINT32 ||
        t == T_UINT64 || t == T_UINTPTR || t == T_INTPTR;
}

static BaseType token_to_basetype(TokenType t) {
    switch (t) {
    case T_CHAR:    return BT_CHAR;
    case T_INT8:    return BT_INT8;
    case T_INT16:   return BT_INT16;
    case T_INT32:   return BT_INT32;
    case T_INT64:   return BT_INT64;
    case T_UINT8:   return BT_UINT8;
    case T_UINT16:  return BT_UINT16;
    case T_UINT32:  return BT_UINT32;
    case T_UINT64:  return BT_UINT64;
    case T_UINTPTR: return BT_UINTPTR;
    case T_INTPTR:  return BT_INTPTR;
    default:        return BT_INT;
    }
}

struct Token {
    TokenType type;
    int64_t   value;
    char      ident[32];
    int       line;
};

static Token token_stream[1024];
static int   token_count = 0;

uint8_t code_buffer[65536];   // powiększony: pętle generują więcej kodu
int     code_idx = 0;

static int my_isdigit(char c) { return c >= '0' && c <= '9'; }
static int my_isalpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int my_isalnum(char c) { return my_isalpha(c) || my_isdigit(c); }
static int decode_escape_char(char c) {
    switch (c) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '\\': return '\\';
    case '\'': return '\'';
    case '0': return '\0';
    default: return c;
    }
}

void lex(const char* src) {
    token_count = 0;
    errors_reset();
    int t = 0, line = 1;

    while (*src) {
        if (*src == '\n') { line++; src++; continue; }
        if ((uint8_t)*src <= ' ') { src++; continue; }
        if (src[0] == '/' && src[1] == '/') { while (*src && *src != '\n') src++; continue; }

        if (t >= 1023) { add_error(line, "Za duzo tokenow (limit 1023)"); break; }

        token_stream[t].line = line;

        if (my_isalpha(*src)) {
            char buf[32]; int len = 0;
            while (my_isalnum(*src) && len < 31) buf[len++] = *src++;
            buf[len] = '\0';

            if (my_strcmp(buf, "return") == 0) token_stream[t].type = T_RETURN;
            else if (my_strcmp(buf, "if") == 0) token_stream[t].type = T_IF;
            else if (my_strcmp(buf, "else") == 0) token_stream[t].type = T_ELSE;
            else if (my_strcmp(buf, "while") == 0) token_stream[t].type = T_WHILE;
            else if (my_strcmp(buf, "do") == 0) token_stream[t].type = T_DO;
            else if (my_strcmp(buf, "for") == 0) token_stream[t].type = T_FOR;
            else if (my_strcmp(buf, "char") == 0) token_stream[t].type = T_CHAR;
            else if (my_strcmp(buf, "int") == 0) token_stream[t].type = T_INT;
            else if (my_strcmp(buf, "int8_t") == 0) token_stream[t].type = T_INT8;
            else if (my_strcmp(buf, "int16_t") == 0) token_stream[t].type = T_INT16;
            else if (my_strcmp(buf, "int32_t") == 0) token_stream[t].type = T_INT32;
            else if (my_strcmp(buf, "int64_t") == 0) token_stream[t].type = T_INT64;
            else if (my_strcmp(buf, "uint8_t") == 0) token_stream[t].type = T_UINT8;
            else if (my_strcmp(buf, "uint16_t") == 0) token_stream[t].type = T_UINT16;
            else if (my_strcmp(buf, "uint32_t") == 0) token_stream[t].type = T_UINT32;
            else if (my_strcmp(buf, "uint64_t") == 0) token_stream[t].type = T_UINT64;
            else if (my_strcmp(buf, "uintptr_t") == 0) token_stream[t].type = T_UINTPTR;
            else if (my_strcmp(buf, "intptr_t") == 0) token_stream[t].type = T_INTPTR;
            else {
                token_stream[t].type = T_IDENT;
                my_strcpy(token_stream[t].ident, buf, 32);
            }
            t++; continue;
        }

        if (my_isdigit(*src)) {
            int64_t val = 0;
            if (src[0] == '0' && (src[1] == 'x' || src[1] == 'X')) {
                src += 2;
                while ((*src >= '0' && *src <= '9') || (*src >= 'a' && *src <= 'f') || (*src >= 'A' && *src <= 'F')) {
                    int d = (*src >= '0' && *src <= '9') ? *src - '0'
                        : (*src >= 'a' && *src <= 'f') ? *src - 'a' + 10 : *src - 'A' + 10;
                    val = val * 16 + d; src++;
                }
            }
            else {
                while (my_isdigit(*src)) { val = val * 10 + (*src - '0'); src++; }
            }
            token_stream[t].type = T_NUMBER;
            token_stream[t].value = val;
            t++; continue;
        }

        if (*src == '\'') {
            src++;
            if (!*src) { add_error(line, "Niedomkniety literał znakowy"); break; }

            int value = 0;
            if (*src == '\\') {
                src++;
                if (!*src) { add_error(line, "Niepoprawny escape w literałe znakowym"); break; }
                value = decode_escape_char(*src++);
            }
            else {
                value = (uint8_t)*src++;
            }

            if (*src != '\'') {
                add_error(line, "Literał znakowy musi zawierać dokładnie jeden znak");
                while (*src && *src != '\'' && *src != '\n') src++;
                if (*src == '\'') src++;
            }
            else {
                src++;
            }

            token_stream[t].type = T_NUMBER;
            token_stream[t].value = value;
            t++; continue;
        }

        switch (*src) {
        case '+':
            if (src[1] == '+') { token_stream[t].type = T_PLUSPLUS;  src += 2; }
            else if (src[1] == '=') { token_stream[t].type = T_PLUSEQ;    src += 2; }
            else { token_stream[t].type = T_PLUS;       src++; }
            break;
        case '-':
            if (src[1] == '-') { token_stream[t].type = T_MINUSMINUS; src += 2; }
            else if (src[1] == '=') { token_stream[t].type = T_MINUSEQ;    src += 2; }
            else { token_stream[t].type = T_MINUS;       src++; }
            break;
        case '*':
            if (src[1] == '=') { token_stream[t].type = T_STAREQ;   src += 2; }
            else { token_stream[t].type = T_STAR;      src++; }
            break;
        case '/':
            if (src[1] == '=') { token_stream[t].type = T_SLASHEQ;  src += 2; }
            else { token_stream[t].type = T_SLASH;     src++; }
            break;
        case '%':
            if (src[1] == '=') { token_stream[t].type = T_PERCENTEQ; src += 2; }
            else { token_stream[t].type = T_PERCENT;   src++; }
            break;
        case '=':
            if (src[1] == '=') { token_stream[t].type = T_EQ;     src += 2; }
            else { token_stream[t].type = T_ASSIGN;  src++; }
            break;
        case '!':
            if (src[1] == '=') { token_stream[t].type = T_NEQ;  src += 2; }
            else { token_stream[t].type = T_NOT;   src++; }
            break;
        case '<':
            if (src[1] == '=') { token_stream[t].type = T_LEQ;  src += 2; }
            else { token_stream[t].type = T_LT;   src++; }
            break;
        case '>':
            if (src[1] == '=') { token_stream[t].type = T_GEQ;  src += 2; }
            else { token_stream[t].type = T_GT;   src++; }
            break;
        case '&':
            if (src[1] == '&') { token_stream[t].type = T_AND;       src += 2; }
            else { token_stream[t].type = T_AMPERSAND; src++; }
            break;
        case '|':
            if (src[1] == '|') { token_stream[t].type = T_OR;   src += 2; }
            else { token_stream[t].type = T_PIPE; src++; }
            break;
        case ';': token_stream[t].type = T_SEMICOLON; src++; break;
        case '(': token_stream[t].type = T_LPAREN;    src++; break;
        case ')': token_stream[t].type = T_RPAREN;    src++; break;
        case '[': token_stream[t].type = T_LBRACKET;  src++; break;
        case ']': token_stream[t].type = T_RBRACKET;  src++; break;
        case '{': token_stream[t].type = T_LBRACE;    src++; break;
        case '}': token_stream[t].type = T_RBRACE;    src++; break;
        default: {
            char msg[64] = "Nieznany znak: '";
            char ch[2] = { *src, '\0' };
            my_strcat(msg, ch, 64); my_strcat(msg, "'", 64);
            add_error(line, msg);
            token_stream[t].type = T_UNKNOWN; src++; break;
        }
        }
        token_stream[t].line = line;
        t++;
    }

    token_stream[t].type = T_EOF;
    token_stream[t].line = line;
    token_count = t;
}

// ─────────────────────────────────────────
//  TABELA ZMIENNYCH
// ─────────────────────────────────────────

struct Var {
    char    name[32];
    int     offset;
    VarType vtype;
    bool    initialized;
    int     elem_count;
};

static Var  var_table[64];
static int  var_count = 0;
static int  stack_size = 0;

static void vars_reset() { var_count = 0; stack_size = 0; }

static Var* var_find_ptr(const char* name) {
    for (int i = 0; i < var_count; i++)
        if (my_strcmp(var_table[i].name, name) == 0) return &var_table[i];
    return nullptr;
}

static int var_find(const char* name) {
    Var* v = var_find_ptr(name); return v ? v->offset : -1;
}

static VarType var_get_type(const char* name) {
    Var* v = var_find_ptr(name); return v ? v->vtype : type_int();
}

static bool var_is_initialized(const char* name) {
    Var* v = var_find_ptr(name); return v ? v->initialized : false;
}

static int align_up(int value, int align) {
    return (value + align - 1) & ~(align - 1);
}

static bool var_is_array(const char* name) {
    Var* v = var_find_ptr(name); return v ? (v->elem_count > 0) : false;
}

static int var_elem_count(const char* name) {
    Var* v = var_find_ptr(name); return v ? v->elem_count : 0;
}

static int vartype_storage_size(VarType vt, int elem_count) {
    int element_size = basetype_size(vt.base);
    int total = (elem_count > 0) ? (element_size * elem_count) : element_size;
    if (total < 8) total = 8;
    return align_up(total, 8);
}

static int var_alloc(const char* name, VarType vt, int elem_count = 0) {
    int storage = vartype_storage_size(vt, elem_count);
    stack_size += storage;
    var_table[var_count] = { {}, stack_size, vt, false, elem_count };
    my_strcpy(var_table[var_count].name, name, 32);
    return var_table[var_count++].offset;
}

static void var_mark_initialized(const char* name) {
    Var* v = var_find_ptr(name); if (v) v->initialized = true;
}

// ─────────────────────────────────────────
//  EMITTER
// ─────────────────────────────────────────

static void emit_byte(uint8_t b) {
    if (code_idx < 65536) code_buffer[code_idx++] = b;
}

static void emit_i32(int v) {
    emit_byte(v & 0xFF); emit_byte((v >> 8) & 0xFF);
    emit_byte((v >> 16) & 0xFF); emit_byte((v >> 24) & 0xFF);
}

static void emit_i64(int64_t v) {
    for (int i = 0; i < 8; i++) emit_byte((uint8_t)((v >> (i * 8)) & 0xFF));
}

static void emit_push_rbp() { emit_byte(0x55); }
static void emit_mov_rbp_rsp() { emit_byte(0x48); emit_byte(0x89); emit_byte(0xE5); }
static void emit_mov_rsp_rbp() { emit_byte(0x48); emit_byte(0x89); emit_byte(0xEC); }
static void emit_pop_rbp() { emit_byte(0x5D); }
static void emit_ret() { emit_byte(0xC3); }

static void emit_sub_rsp(int n) {
    emit_byte(0x48); emit_byte(0x81); emit_byte(0xEC); emit_i32(n);
}

static void emit_mov_rax_imm64(int64_t val) {
    if (val >= -2147483648LL && val <= 2147483647LL) {
        emit_byte(0x48); emit_byte(0xC7); emit_byte(0xC0); emit_i32((int)val);
    }
    else {
        emit_byte(0x48); emit_byte(0xB8); emit_i64(val);
    }
}

// mov QWORD [rbp-off], rax
static void emit_store_rax(int off) {
    emit_byte(0x48); emit_byte(0x89); emit_byte(0x85); emit_i32(-off);
}

// mov rax, QWORD [rbp-off]
static void emit_load_rax(int off) {
    emit_byte(0x48); emit_byte(0x8B); emit_byte(0x85); emit_i32(-off);
}

static void emit_store_rax_sized(int off, int sz) {
    switch (sz) {
    case 1: emit_byte(0x88); emit_byte(0x85); emit_i32(-off); break;
    case 2: emit_byte(0x66); emit_byte(0x89); emit_byte(0x85); emit_i32(-off); break;
    case 4: emit_byte(0x89); emit_byte(0x85); emit_i32(-off); break;
    default: emit_store_rax(off); break;
    }
}

static void emit_load_rax_sized(int off, int sz, bool sgn) {
    switch (sz) {
    case 1:
        emit_byte(0x48); emit_byte(0x0F); emit_byte(sgn ? 0xBE : 0xB6); emit_byte(0x85); emit_i32(-off);
        break;
    case 2:
        emit_byte(0x48); emit_byte(0x0F); emit_byte(sgn ? 0xBF : 0xB7); emit_byte(0x85); emit_i32(-off);
        break;
    case 4:
        if (sgn) { emit_byte(0x48); emit_byte(0x63); emit_byte(0x85); emit_i32(-off); }
        else { emit_byte(0x8B); emit_byte(0x85); emit_i32(-off); }
        break;
    default:
        emit_load_rax(off);
        break;
    }
}

// lea rax, [rbp-off]
static void emit_lea_rax(int off) {
    emit_byte(0x48); emit_byte(0x8D); emit_byte(0x85); emit_i32(-off);
}

static void emit_push_rax() { emit_byte(0x50); }
static void emit_pop_rcx() { emit_byte(0x59); }

// mov [rax], rcx  (różne rozmiary)
static void emit_store_rcx_via_rax(int sz) {
    switch (sz) {
    case 1: emit_byte(0x88); emit_byte(0x08); break;
    case 2: emit_byte(0x66); emit_byte(0x89); emit_byte(0x08); break;
    case 4: emit_byte(0x89); emit_byte(0x08); break;
    default:emit_byte(0x48); emit_byte(0x89); emit_byte(0x08); break;
    }
}

// mov rax, [rax]  (różne rozmiary, sign/zero-extend)
static void emit_load_via_rax(int sz, bool sgn) {
    switch (sz) {
    case 1: emit_byte(0x48); emit_byte(0x0F);
        emit_byte(sgn ? 0xBE : 0xB6); emit_byte(0x00); break;
    case 2: emit_byte(0x48); emit_byte(0x0F);
        emit_byte(sgn ? 0xBF : 0xB7); emit_byte(0x00); break;
    case 4: if (sgn) { emit_byte(0x48); emit_byte(0x63); emit_byte(0x00); }
          else { emit_byte(0x8B); emit_byte(0x00); } break;
    default:emit_byte(0x48); emit_byte(0x8B); emit_byte(0x00); break;
    }
}

static void emit_add_rax_rcx() { emit_byte(0x48); emit_byte(0x01); emit_byte(0xC8); }

static void emit_sub_rcx_rax() {
    // rcx - rax → rax
    emit_byte(0x48); emit_byte(0x29); emit_byte(0xC1); // sub rcx, rax
    emit_byte(0x48); emit_byte(0x89); emit_byte(0xC8); // mov rax, rcx
}

static void emit_imul_rax_rcx() {
    emit_byte(0x48); emit_byte(0x0F); emit_byte(0xAF); emit_byte(0xC1);
}

// idiv: rax = rcx / rax,  rdx = rcx % rax
// Wejście: lewy operand w rcx, prawy w rax
static void emit_div_prepare() {
    emit_byte(0x48); emit_byte(0x91); // xchg rax, rcx  → rax=lewy, rcx=prawy
    emit_byte(0x48); emit_byte(0x99); // cqo (sign-extend rax → rdx:rax)
    emit_byte(0x48); emit_byte(0xF7); emit_byte(0xF9); // idiv rcx
    // wynik: rax = iloraz, rdx = reszta
}

// Po emit_div_prepare() — przenieś resztę do rax
static void emit_mod_to_rax() {
    emit_byte(0x48); emit_byte(0x89); emit_byte(0xD0); // mov rax, rdx
}

static void emit_imul_rax_imm(int imm) {
    emit_byte(0x48); emit_byte(0x69); emit_byte(0xC0); emit_i32(imm);
}

static void emit_cmp_rcx_rax() { emit_byte(0x48); emit_byte(0x39); emit_byte(0xC1); }

static void emit_setcc_to_rax(uint8_t op) {
    emit_byte(0x0F); emit_byte(op); emit_byte(0xC0);   // setXX al
    emit_byte(0x48); emit_byte(0x0F); emit_byte(0xB6); emit_byte(0xC0); // movzx rax,al
}

// test rax, rax  → rax = (rax != 0) ? 1 : 0
static void emit_bool_normalize_rax() {
    emit_byte(0x48); emit_byte(0x85); emit_byte(0xC0); // test rax, rax
    emit_byte(0x0F); emit_byte(0x95); emit_byte(0xC0); // setnz al
    emit_byte(0x48); emit_byte(0x0F); emit_byte(0xB6); emit_byte(0xC0); // movzx rax, al
}

// and rax, rcx
static void emit_and_rax_rcx() { emit_byte(0x48); emit_byte(0x21); emit_byte(0xC8); }
// or  rax, rcx
static void emit_or_rax_rcx() { emit_byte(0x48); emit_byte(0x09); emit_byte(0xC8); }

// inc/dec QWORD [rbp-off]
static void emit_inc_mem(int off) {
    emit_byte(0x48); emit_byte(0xFF); emit_byte(0x85); emit_i32(-off);
}
static void emit_dec_mem(int off) {
    emit_byte(0x48); emit_byte(0xFF); emit_byte(0x8D); emit_i32(-off);
}

static void emit_inc_mem_sized(int off, int sz) {
    switch (sz) {
    case 1: emit_byte(0xFE); emit_byte(0x85); emit_i32(-off); break;
    case 2: emit_byte(0x66); emit_byte(0xFF); emit_byte(0x85); emit_i32(-off); break;
    case 4: emit_byte(0xFF); emit_byte(0x85); emit_i32(-off); break;
    default: emit_inc_mem(off); break;
    }
}

static void emit_dec_mem_sized(int off, int sz) {
    switch (sz) {
    case 1: emit_byte(0xFE); emit_byte(0x8D); emit_i32(-off); break;
    case 2: emit_byte(0x66); emit_byte(0xFF); emit_byte(0x8D); emit_i32(-off); break;
    case 4: emit_byte(0xFF); emit_byte(0x8D); emit_i32(-off); break;
    default: emit_dec_mem(off); break;
    }
}

static int emit_jmp() { emit_byte(0xE9); int p = code_idx; emit_i32(0); return p; }

// jmp назад до адреса target (уже известного)
static void emit_jmp_back(int target) {
    emit_byte(0xE9);
    int off = target - (code_idx + 4);
    emit_i32(off);
}

static int emit_jz() {
    emit_byte(0x48); emit_byte(0x85); emit_byte(0xC0); // test rax, rax
    emit_byte(0x0F); emit_byte(0x84); int p = code_idx; emit_i32(0); return p;
}

static int emit_jnz() {
    emit_byte(0x48); emit_byte(0x85); emit_byte(0xC0); // test rax, rax
    emit_byte(0x0F); emit_byte(0x85); int p = code_idx; emit_i32(0); return p;
}

static void patch_jmp(int pp) {
    int off = code_idx - (pp + 4);
    code_buffer[pp + 0] = (uint8_t)(off & 0xFF);
    code_buffer[pp + 1] = (uint8_t)((off >> 8) & 0xFF);
    code_buffer[pp + 2] = (uint8_t)((off >> 16) & 0xFF);
    code_buffer[pp + 3] = (uint8_t)((off >> 24) & 0xFF);
}

// ─────────────────────────────────────────
//  PARSER + CODEGEN
// ─────────────────────────────────────────

static int  curr = 0;
static bool expr_ok = true;

static TokenType peek() { return token_stream[curr].type; }
static int       peek_line() { return token_stream[curr].line; }
static Token     consume() { return token_stream[curr++]; }

static bool is_keyword(TokenType t) {
    return t == T_RETURN || t == T_IF || t == T_ELSE ||
        t == T_WHILE || t == T_DO || t == T_FOR || is_type_token(t);
}

static bool expect(TokenType t) {
    if (peek() == t) { consume(); return true; }
    char msg[128] = "Oczekiwano ";
    my_strcat(msg, token_name(t), 128);
    my_strcat(msg, ", znaleziono: ", 128);
    my_strcat(msg, token_name(peek()), 128);
    add_error(peek_line(), msg);
    return false;
}

static VarType parse_type() {
    BaseType b = token_to_basetype(consume().type);
    bool ptr = (peek() == T_STAR);
    if (ptr) consume();
    return make_type(b, ptr, 0);
}

static int parse_array_suffix() {
    if (peek() != T_LBRACKET) return 0;
    consume();
    if (peek() != T_NUMBER) {
        add_error(peek_line(), "Rozmiar tablicy musi byc stalym literałem liczbowym");
        expect(T_RBRACKET);
        return -1;
    }
    int len = (int)consume().value;
    if (len <= 0) add_error(peek_line(), "Rozmiar tablicy musi byc dodatni");
    expect(T_RBRACKET);
    return len;
}

static VarType scalar_type_of(VarType vt) {
    return make_type(vt.base, vt.is_ptr, 0);
}

static VarType pointer_to_base_of(VarType vt) {
    return make_type(vt.base, true, 0);
}

static int vartype_value_size(VarType vt) {
    if (vt.is_ptr) return 8;
    return basetype_size(vt.base);
}

static void emit_store_rax_var(int off, VarType vt) {
    emit_store_rax_sized(off, vartype_value_size(vt));
}

static void emit_load_var_to_rax(int off, VarType vt) {
    emit_load_rax_sized(off, vartype_value_size(vt), basetype_is_signed(vt.base));
}

static void emit_inc_var(int off, VarType vt) {
    emit_inc_mem_sized(off, vartype_value_size(vt));
}

static void emit_dec_var(int off, VarType vt) {
    emit_dec_mem_sized(off, vartype_value_size(vt));
}

static VarType expr();

static void emit_index_address_from_rax(VarType base_type) {
    int sz = basetype_size(base_type.base);
    emit_push_rax();
    expr();
    if (sz > 1) emit_imul_rax_imm(sz);
    emit_pop_rcx();
    emit_add_rax_rcx();
}

static bool is_cast_ahead() {
    if (peek() != T_LPAREN) return false;
    int s = curr; curr++;
    if (!is_type_token(peek())) { curr = s; return false; }
    curr++;
    if (peek() == T_STAR) curr++;
    bool ok = (peek() == T_RPAREN);
    curr = s; return ok;
}

static void    statement();
static void    block();
static VarType expr();

// ── compound assign helper ────────────────
// Emituje kod dla: varname OP= expr
// Zakłada że wartość wyrażenia prawej strony jest już w rax.
// Ładuje starą wartość zmiennej do rcx, wykonuje op, zapisuje wynik.
static void emit_compound_assign(const char* name, TokenType op, int off) {
    VarType vt = scalar_type_of(var_get_type(name));
    // rax = nowa wartość (prawa strona)
    emit_push_rax();              // stos: [nowa]
    emit_load_var_to_rax(off, vt);// rax = stara wartość
    emit_pop_rcx();               // rcx = nowa wartość
    // teraz: rax=stara, rcx=nowa
    switch (op) {
    case T_PLUSEQ:    emit_add_rax_rcx(); break;      // rax = stara + nowa
    case T_MINUSEQ:
        // stara - nowa: rax=stara w rax, nowa w rcx → sub rax,rcx
        emit_byte(0x48); emit_byte(0x29); emit_byte(0xC8); // sub rax, rcx
        break;
    case T_STAREQ:    emit_imul_rax_rcx(); break;
    case T_SLASHEQ:
        // rax=stara (lewy), rcx=nowy (prawy) → xchg, cqo, idiv
        // już mamy: rax=stara, rcx=nowa
        emit_byte(0x48); emit_byte(0x99); // cqo
        emit_byte(0x48); emit_byte(0xF7); emit_byte(0xF9); // idiv rcx
        break;
    case T_PERCENTEQ:
        emit_byte(0x48); emit_byte(0x99); // cqo
        emit_byte(0x48); emit_byte(0xF7); emit_byte(0xF9); // idiv rcx → rdx=reszta
        emit_mod_to_rax();
        break;
    default: break;
    }
    emit_store_rax_var(off, vt);
}

// ── primary ───────────────────────────────
static VarType primary() {

    // cast: (TYPE[*]) primary
    if (is_cast_ahead()) {
        consume(); // '('
        VarType ct = parse_type();
        consume(); // ')'
        primary();
        if (!ct.is_ptr) {
            int sz = basetype_size(ct.base);
            if (sz == 1) { emit_byte(0x48); emit_byte(0x25); emit_i32(0xFF); }
            else if (sz == 2) { emit_byte(0x48); emit_byte(0x25); emit_i32(0xFFFF); }
            else if (sz == 4) { emit_byte(0x89); emit_byte(0xC0); } // zero-extend
        }
        return ct;
    }

    // !expr  (logiczne NOT)
    if (peek() == T_NOT) {
        consume();
        primary();
        emit_bool_normalize_rax();
        // not: 0→1, nonzero→0
        emit_byte(0x48); emit_byte(0x83); emit_byte(0xF0); emit_byte(0x01); // xor rax, 1
        return type_int();
    }

    // ++ident  (pre-increment)
    if (peek() == T_PLUSPLUS) {
        int line = peek_line(); consume();
        if (peek() != T_IDENT) { add_error(line, "Oczekiwano nazwy zmiennej po '++'"); expr_ok = false; return type_int(); }
        char name[32]; my_strcpy(name, consume().ident, 32);
        Var* v = var_find_ptr(name);
        if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); expr_ok = false; return type_int(); }
        if (v->elem_count > 0) { add_error_tok(line, "Nie mozna inkrementowac tablicy: ", name); expr_ok = false; return type_int(); }
        emit_inc_var(v->offset, v->vtype);
        emit_load_var_to_rax(v->offset, v->vtype);
        var_mark_initialized(name);
        return scalar_type_of(v->vtype);
    }

    // --ident  (pre-decrement)
    if (peek() == T_MINUSMINUS) {
        int line = peek_line(); consume();
        if (peek() != T_IDENT) { add_error(line, "Oczekiwano nazwy zmiennej po '--'"); expr_ok = false; return type_int(); }
        char name[32]; my_strcpy(name, consume().ident, 32);
        Var* v = var_find_ptr(name);
        if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); expr_ok = false; return type_int(); }
        if (v->elem_count > 0) { add_error_tok(line, "Nie mozna dekrementowac tablicy: ", name); expr_ok = false; return type_int(); }
        emit_dec_var(v->offset, v->vtype);
        emit_load_var_to_rax(v->offset, v->vtype);
        var_mark_initialized(name);
        return scalar_type_of(v->vtype);
    }

    // liczba
    if (peek() == T_NUMBER) {
        emit_mov_rax_imm64(consume().value);
        return type_int();
    }

    // identyfikator, tablica lub indeksowanie
    if (peek() == T_IDENT) {
        char name[32]; int line = peek_line();
        my_strcpy(name, consume().ident, 32);
        Var* v = var_find_ptr(name);
        if (!v) {
            add_error_tok(line, "Niezadeklarowana zmienna: ", name);
            emit_mov_rax_imm64(0); expr_ok = false; return type_int();
        }
        if (!v->initialized && v->elem_count == 0) {
            add_error_tok(line, "Zmienna uzyta przed inicjalizacja: ", name);
            emit_mov_rax_imm64(0); expr_ok = false; return type_int();
        }
        VarType vt = v->vtype;

        if (peek() == T_LBRACKET) {
            if (!vt.is_ptr && v->elem_count == 0) {
                add_error_tok(line, "Indeksowanie nie-tablicy i nie-wskaznika: ", name);
                emit_mov_rax_imm64(0); expr_ok = false; return type_int();
            }

            if (v->elem_count > 0) emit_lea_rax(v->offset);
            else emit_load_var_to_rax(v->offset, vt);
            consume(); // '['
            emit_index_address_from_rax(pointer_to_base_of(vt));
            expect(T_RBRACKET);
            emit_load_via_rax(basetype_size(vt.base), basetype_is_signed(vt.base));
            return make_type(vt.base, false, 0);
        }

        if (v->elem_count > 0) {
            emit_lea_rax(v->offset);
            return pointer_to_base_of(vt);
        }

        emit_load_var_to_rax(v->offset, vt);

        // post-increment: i++
        if (peek() == T_PLUSPLUS) {
            consume();
            // rax ma starą wartość — to jest wynik wyrażenia
            emit_inc_var(v->offset, vt);
            return scalar_type_of(vt);
        }
        // post-decrement: i--
        if (peek() == T_MINUSMINUS) {
            consume();
            emit_dec_var(v->offset, vt);
            return scalar_type_of(vt);
        }
        return scalar_type_of(vt);
    }

    // dereferencja: *expr
    if (peek() == T_STAR) {
        int line = peek_line(); consume();
        VarType pt = primary();
        if (!pt.is_ptr) { add_error(line, "Dereferencja nie-wskaznika"); expr_ok = false; return type_int(); }
        emit_load_via_rax(basetype_size(pt.base), basetype_is_signed(pt.base));
        return make_type(pt.base, false, 0);
    }

    // adres: &ident
    if (peek() == T_AMPERSAND) {
        int line = peek_line(); consume();
        if (peek() != T_IDENT) { add_error(line, "Po '&' oczekiwano nazwy zmiennej"); expr_ok = false; return type_int(); }
        char name[32]; my_strcpy(name, consume().ident, 32);
        int off = var_find(name);
        if (off < 0) { add_error_tok(line, "Niezadeklarowana zmienna przy &: ", name); expr_ok = false; return type_int(); }
        emit_lea_rax(off);
        VarType bt = var_get_type(name);
        return make_type(bt.base, true, 0);
    }

    // nawiasy
    if (peek() == T_LPAREN) {
        consume();
        VarType t = expr();
        expect(T_RPAREN);
        return t;
    }

    if (is_keyword(peek())) {
        add_error_tok(peek_line(), "Slowo kluczowe w miejscu wyrazenia: ", token_name(peek()));
        consume(); expr_ok = false; return type_int();
    }

    add_error_tok(peek_line(), "Nieoczekiwany token w wyrazeniu: ", token_name(peek()));
    consume(); expr_ok = false; return type_int();
}

// ── unary minus ───────────────────────────
static VarType unary() {
    if (peek() == T_MINUS) {
        consume();
        primary();
        // neg rax
        emit_byte(0x48); emit_byte(0xF7); emit_byte(0xD8);
        return type_int();
    }
    return primary();
}

// ── term (* / %) ──────────────────────────
static VarType term() {
    VarType t = unary();
    while (peek() == T_STAR || peek() == T_SLASH || peek() == T_PERCENT) {
        int line = peek_line();
        TokenType op = consume().type;
        if ((op == T_SLASH || op == T_PERCENT) &&
            peek() == T_NUMBER && token_stream[curr].value == 0) {
            add_error(line, "Dzielenie przez zero"); consume(); expr_ok = false; return t;
        }
        emit_push_rax();
        unary();
        emit_pop_rcx(); // lewy w rcx, prawy w rax
        if (op == T_STAR) {
            emit_imul_rax_rcx();
        }
        else {
            emit_div_prepare(); // rax=iloraz, rdx=reszta
            if (op == T_PERCENT) emit_mod_to_rax();
        }
        t = type_int();
    }
    return t;
}

// ── addexpr (+ - z arytmetyką wskaźników) ─
static VarType addexpr() {
    VarType t = term();
    while (peek() == T_PLUS || peek() == T_MINUS) {
        TokenType op = consume().type;
        emit_push_rax();
        VarType t2 = term();
        emit_pop_rcx(); // lewy w rcx, prawy w rax

        if (t.is_ptr && !t2.is_ptr) {
            int sz = pointee_size(t);
            if (sz > 1) emit_imul_rax_imm(sz);
            if (op == T_PLUS) emit_add_rax_rcx();
            else              emit_sub_rcx_rax();
        }
        else if (!t.is_ptr && t2.is_ptr && op == T_PLUS) {
            int sz = pointee_size(t2);
            emit_byte(0x48); emit_byte(0x91); // xchg rax, rcx
            if (sz > 1) { emit_byte(0x48); emit_byte(0x69); emit_byte(0xC9); emit_i32(sz); }
            emit_add_rax_rcx();
            t = t2;
        }
        else if (t.is_ptr && t2.is_ptr && op == T_MINUS) {
            int sz = pointee_size(t);
            emit_sub_rcx_rax();
            if (sz > 1) {
                emit_byte(0x48); emit_byte(0xC7); emit_byte(0xC1); emit_i32(sz);
                emit_byte(0x48); emit_byte(0x99);
                emit_byte(0x48); emit_byte(0xF7); emit_byte(0xF9);
            }
            t = type_int();
        }
        else {
            if (op == T_PLUS) emit_add_rax_rcx();
            else              emit_sub_rcx_rax();
            t = type_int();
        }
    }
    return t;
}

// ── relexpr (< > <= >=) ───────────────────
static VarType relexpr() {
    VarType t = addexpr();
    TokenType op = peek();
    if (op == T_LT || op == T_GT || op == T_LEQ || op == T_GEQ) {
        consume();
        emit_push_rax();
        addexpr();
        emit_pop_rcx();
        emit_cmp_rcx_rax();
        switch (op) {
        case T_LT:  emit_setcc_to_rax(0x9C); break;
        case T_GT:  emit_setcc_to_rax(0x9F); break;
        case T_LEQ: emit_setcc_to_rax(0x9E); break;
        case T_GEQ: emit_setcc_to_rax(0x9D); break;
        default: break;
        }
        return type_int();
    }
    return t;
}

// ── eqexpr (== !=) ────────────────────────
static VarType eqexpr() {
    VarType t = relexpr();
    TokenType op = peek();
    if (op == T_EQ || op == T_NEQ) {
        consume();
        emit_push_rax();
        relexpr();
        emit_pop_rcx();
        emit_cmp_rcx_rax();
        if (op == T_EQ) emit_setcc_to_rax(0x94);
        else            emit_setcc_to_rax(0x95);
        return type_int();
    }
    return t;
}

// ── andexpr (&&) ──────────────────────────
// Implementacja short-circuit: jeśli lewa strona jest 0, prawa nie jest ewaluowana.
static VarType andexpr() {
    VarType t = eqexpr();
    while (peek() == T_AND) {
        consume();
        emit_bool_normalize_rax();  // lewa: 0 lub 1 w rax
        emit_push_rax();
        // jeśli lewa == 0, skocz do końca (short-circuit)
        int jz = emit_jz();
        eqexpr();
        emit_bool_normalize_rax();  // prawa: 0 lub 1 w rax
        emit_pop_rcx();             // rcx = lewa
        emit_and_rax_rcx();         // rax = lewa & prawa (oba są 0/1)
        patch_jmp(jz);
        t = type_int();
    }
    return t;
}

// ── orexpr (||) ───────────────────────────
// Short-circuit: jeśli lewa != 0, prawa nie jest ewaluowana.
static VarType orexpr() {
    VarType t = andexpr();
    while (peek() == T_OR) {
        consume();
        emit_bool_normalize_rax();  // lewa: 0 lub 1
        emit_push_rax();
        // jeśli lewa != 0, skocz do końca
        int jnz = emit_jnz();
        andexpr();
        emit_bool_normalize_rax();
        emit_pop_rcx();
        emit_or_rax_rcx();
        patch_jmp(jnz);
        t = type_int();
    }
    return t;
}

// ── expr (pełne wyrażenie) ────────────────
static VarType expr() {
    expr_ok = true;
    VarType t = orexpr();

    if (peek() == T_ASSIGN) {
        add_error(peek_line(), "Uzyto '=' zamiast '==' — przypisanie nie jest wyrazeniem");
        consume(); orexpr(); expr_ok = false; return t;
    }
    return t;
}

// ── block ─────────────────────────────────
static void block() {
    if (peek() == T_LBRACE) {
        consume();
        while (peek() != T_RBRACE && peek() != T_EOF) statement();
        expect(T_RBRACE);
    }
    else {
        statement();
    }
}

// ── for_init: deklaracja lub przypisanie bez średnika ─
// Używane w nagłówku for(init; cond; post)
static void for_init() {
    if (peek() == T_SEMICOLON) return; // pusty init

    if (is_type_token(peek())) {
        // deklaracja zmiennej z opcjonalną inicjalizacją
        int line = peek_line();
        VarType vt = parse_type();
        if (peek() != T_IDENT) { add_error(line, "Po typie oczekiwano nazwy zmiennej"); return; }
        char name[32]; my_strcpy(name, consume().ident, 32);
        if (var_find(name) >= 0) { add_error_tok(line, "Ponowna deklaracja zmiennej: ", name); return; }
        int array_len = parse_array_suffix();
        if (array_len < 0) return;
        vt.array_len = array_len;
        int off = var_alloc(name, vt, array_len);
        if (array_len > 0) {
            var_mark_initialized(name);
        }
        if (peek() == T_ASSIGN) {
            if (array_len > 0) { add_error(line, "Inicjalizacja tablicy pojedynczym wyrazeniem nie jest obslugiwana"); return; }
            consume();
            expr();
            emit_store_rax_var(off, vt);
            var_mark_initialized(name);
        }
        return;
    }

    // przypisanie lub compound assign
    if (peek() == T_IDENT) {
        char name[32]; int line = peek_line();
        my_strcpy(name, token_stream[curr].ident, 32);
        TokenType next = token_stream[curr + 1].type;

        if (next == T_ASSIGN) {
            consume(); consume();
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); return; }
            if (v->elem_count > 0) { add_error_tok(line, "Nie mozna przypisac do calej tablicy: ", name); return; }
            expr(); emit_store_rax_var(v->offset, v->vtype); var_mark_initialized(name);
            return;
        }
        if (next == T_PLUSEQ || next == T_MINUSEQ || next == T_STAREQ ||
            next == T_SLASHEQ || next == T_PERCENTEQ) {
            consume();
            TokenType op = consume().type;
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); return; }
            if (v->elem_count > 0) { add_error_tok(line, "Nie mozna modyfikowac calej tablicy: ", name); return; }
            expr();
            emit_compound_assign(name, op, v->offset);
            return;
        }
    }
    // fallback: dowolne wyrażenie
    expr();
}

// ── for_post: wyrażenie w trzeciej części for() ─
static void for_post() {
    if (peek() == T_RPAREN) return;

    if (peek() == T_IDENT) {
        char name[32]; int line = peek_line();
        my_strcpy(name, token_stream[curr].ident, 32);
        TokenType next = token_stream[curr + 1].type;

        // i++, i--
        if (next == T_PLUSPLUS) {
            consume(); consume();
            Var* v = var_find_ptr(name);
            if (v && v->elem_count == 0) emit_inc_var(v->offset, v->vtype);
            return;
        }
        if (next == T_MINUSMINUS) {
            consume(); consume();
            Var* v = var_find_ptr(name);
            if (v && v->elem_count == 0) emit_dec_var(v->offset, v->vtype);
            return;
        }
        // ++i, --i
        if (next == T_ASSIGN || next == T_PLUSEQ || next == T_MINUSEQ ||
            next == T_STAREQ || next == T_SLASHEQ || next == T_PERCENTEQ) {
            consume();
            TokenType op = consume().type;
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); return; }
            if (v->elem_count > 0) { add_error_tok(line, "Nie mozna modyfikowac calej tablicy: ", name); return; }
            expr();
            if (op == T_ASSIGN) { emit_store_rax_var(v->offset, v->vtype); var_mark_initialized(name); }
            else                  emit_compound_assign(name, op, v->offset);
            return;
        }
    }
    if (peek() == T_PLUSPLUS) {
        consume();
        if (peek() == T_IDENT) {
            char name[32]; my_strcpy(name, consume().ident, 32);
            Var* v = var_find_ptr(name);
            if (v && v->elem_count == 0) emit_inc_var(v->offset, v->vtype);
        }
        return;
    }
    if (peek() == T_MINUSMINUS) {
        consume();
        if (peek() == T_IDENT) {
            char name[32]; my_strcpy(name, consume().ident, 32);
            Var* v = var_find_ptr(name);
            if (v && v->elem_count == 0) emit_dec_var(v->offset, v->vtype);
        }
        return;
    }
    expr(); // fallback
}

// ── statement ─────────────────────────────
static void statement() {

    // TYPE [*] IDENT [[N]] [= expr];
    if (is_type_token(peek())) {
        int line = peek_line();
        VarType vt = parse_type();
        if (peek() != T_IDENT) {
            add_error(line, "Po typie oczekiwano nazwy zmiennej");
            while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
            if (peek() == T_SEMICOLON) consume();
            return;
        }
        char name[32]; int nl = peek_line();
        my_strcpy(name, consume().ident, 32);
        if (var_find(name) >= 0) {
            add_error_tok(nl, "Ponowna deklaracja zmiennej: ", name);
            while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
            if (peek() == T_SEMICOLON) consume();
            return;
        }
        int array_len = parse_array_suffix();
        if (array_len < 0) {
            while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
            if (peek() == T_SEMICOLON) consume();
            return;
        }
        vt.array_len = array_len;
        int off = var_alloc(name, vt, array_len);
        if (array_len > 0) var_mark_initialized(name);
        if (peek() == T_ASSIGN) {
            if (array_len > 0) {
                add_error(line, "Inicjalizacja tablicy pojedynczym wyrazeniem nie jest obslugiwana");
                while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
                if (peek() == T_SEMICOLON) consume();
                return;
            }
            consume(); expr(); emit_store_rax_var(off, vt); var_mark_initialized(name);
        }
        expect(T_SEMICOLON);
        return;
    }

    // *ident = expr;
    if (peek() == T_STAR && token_stream[curr + 1].type == T_IDENT) {
        int line = peek_line(); consume();
        char name[32]; my_strcpy(name, consume().ident, 32);
        if (peek() != T_ASSIGN) {
            add_error(line, "Oczekiwano '=' po '*ident'");
            while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
            if (peek() == T_SEMICOLON) consume();
            return;
        }
        consume();
        Var* v = var_find_ptr(name);
        if (!v) { add_error_tok(line, "Niezadeklarowana zmienna przy *: ", name); goto skip_semi; }
        if (!v->vtype.is_ptr) { add_error_tok(line, "Dereferencja nie-wskaznika: ", name);     goto skip_semi; }
        if (!v->initialized) { add_error_tok(line, "Zapis przez niezainicjalizowany wskaznik: ", name); goto skip_semi; }
        expr();
        emit_push_rax();
        emit_load_var_to_rax(v->offset, v->vtype);
        emit_pop_rcx();
        emit_store_rcx_via_rax(basetype_size(v->vtype.base));
        expect(T_SEMICOLON);
        return;
    skip_semi:
        while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
        if (peek() == T_SEMICOLON) consume();
        return;
    }

    // ident = expr; ident[i] = expr; ident OP= expr; ident++/--;
    if (peek() == T_IDENT) {
        char name[32]; int line = peek_line();
        my_strcpy(name, token_stream[curr].ident, 32);
        TokenType next = token_stream[curr + 1].type;

        if (next == T_LBRACKET) {
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); goto skip_semi2; }
            if (!v->vtype.is_ptr && v->elem_count == 0) {
                add_error_tok(line, "Indeksowanie nie-tablicy i nie-wskaznika: ", name);
                goto skip_semi2;
            }

            consume();
            if (v->elem_count > 0) emit_lea_rax(v->offset);
            else emit_load_var_to_rax(v->offset, v->vtype);
            consume();
            emit_index_address_from_rax(pointer_to_base_of(v->vtype));
            expect(T_RBRACKET);
            if (peek() != T_ASSIGN) {
                add_error(line, "Obecnie wspierane jest tylko przypisanie do elementu tablicy/wskaznika");
                goto skip_semi2;
            }
            consume();
            emit_push_rax();
            expr();
            emit_pop_rcx();
            emit_byte(0x48); emit_byte(0x91); // xchg rax, rcx -> rax=adres, rcx=wartosc
            emit_store_rcx_via_rax(basetype_size(v->vtype.base));
            expect(T_SEMICOLON);
            return;
        }

        // ident = expr
        if (next == T_ASSIGN) {
            consume(); consume();
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Przypisanie do niezadeklarowanej zmiennej: ", name); goto skip_semi2; }
            if (v->elem_count > 0) { add_error_tok(line, "Nie mozna przypisac do calej tablicy: ", name); goto skip_semi2; }
            expr(); emit_store_rax_var(v->offset, v->vtype); var_mark_initialized(name);
            expect(T_SEMICOLON); return;
        }

        // ident OP= expr
        if (next == T_PLUSEQ || next == T_MINUSEQ || next == T_STAREQ ||
            next == T_SLASHEQ || next == T_PERCENTEQ) {
            consume();
            TokenType op = consume().type;
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); goto skip_semi2; }
            if (v->elem_count > 0) { add_error_tok(line, "Nie mozna modyfikowac calej tablicy: ", name); goto skip_semi2; }
            if (!var_is_initialized(name)) { add_error_tok(line, "Zmienna uzyta przed inicjalizacja: ", name); goto skip_semi2; }
            expr();
            emit_compound_assign(name, op, v->offset);
            expect(T_SEMICOLON); return;
        }

        // ident++;  lub  ident--;
        if (next == T_PLUSPLUS || next == T_MINUSMINUS) {
            consume();
            TokenType op = consume().type;
            Var* v = var_find_ptr(name);
            if (!v) { add_error_tok(line, "Niezadeklarowana zmienna: ", name); goto skip_semi2; }
            if (v->elem_count > 0) { add_error_tok(line, "Nie mozna inkrementowac/dekrementowac calej tablicy: ", name); goto skip_semi2; }
            if (op == T_PLUSPLUS) emit_inc_var(v->offset, v->vtype);
            else                  emit_dec_var(v->offset, v->vtype);
            expect(T_SEMICOLON); return;
        }
    skip_semi2:
        while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
        if (peek() == T_SEMICOLON) consume();
        return;
    }

    // ++ident;  lub  --ident;  jako instrukcja
    if (peek() == T_PLUSPLUS || peek() == T_MINUSMINUS) {
        int line = peek_line();
        TokenType op = consume().type;

        if (peek() != T_IDENT) {
            add_error(line, "Oczekiwano nazwy zmiennej po ++/--");
            goto skip_semi3;
        }

        { // <--- OTWIERASZ BLOK, aby odizolować zmienne
            char name[32];
            my_strcpy(name, consume().ident, 32);
            Var* v = var_find_ptr(name);

            if (!v) {
                add_error_tok(line, "Niezadeklarowana zmienna: ", name);
                goto skip_semi3;
            }

            if (v->elem_count > 0) {
                add_error_tok(line, "Nie mozna inkrementowac/dekrementowac calej tablicy: ", name);
                goto skip_semi3;
            }

            if (op == T_PLUSPLUS) emit_inc_var(v->offset, v->vtype);
            else                  emit_dec_var(v->offset, v->vtype);

            expect(T_SEMICOLON);
            return;
        } // <--- ZAMYKASZ BLOK

    skip_semi3:
        while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
        if (peek() == T_SEMICOLON) consume();
        return;
    }

    // literał = cokolwiek
    if (peek() == T_NUMBER && token_stream[curr + 1].type == T_ASSIGN) {
        add_error(peek_line(), "Nie mozna przypisac wartosci do literalu liczbowego");
        while (peek() != T_SEMICOLON && peek() != T_EOF) consume();
        if (peek() == T_SEMICOLON) consume();
        return;
    }

    // return [expr];
    if (peek() == T_RETURN) {
        explicit_return_seen = true;
        consume();
        if (peek() == T_SEMICOLON) emit_mov_rax_imm64(0);
        else expr();
        emit_mov_rsp_rbp();
        emit_pop_rbp();
        emit_ret();
        expect(T_SEMICOLON);
        return;
    }

    // ── while (cond) block ────────────────
    if (peek() == T_WHILE) {
        consume();
        expect(T_LPAREN);

        int loop_start = code_idx;          // adres początku warunku

        if (peek() == T_RPAREN) { add_error(peek_line(), "Pusty warunek w while()"); consume(); block(); return; }
        expr();
        expect(T_RPAREN);

        int jz = emit_jz();                 // jeśli warunek == 0, skocz za pętlę
        block();
        emit_jmp_back(loop_start);          // skocz z powrotem do warunku
        patch_jmp(jz);                      // łataj skok wyjścia
        return;
    }

    // ── do block while (cond); ────────────
    if (peek() == T_DO) {
        consume();

        int loop_start = code_idx;          // adres początku ciała

        block();

        expect(T_WHILE);
        expect(T_LPAREN);
        if (peek() == T_RPAREN) { add_error(peek_line(), "Pusty warunek w do-while()"); consume(); expect(T_SEMICOLON); return; }
        expr();
        expect(T_RPAREN);
        expect(T_SEMICOLON);

        // jeśli warunek != 0, skocz z powrotem
        emit_byte(0x48); emit_byte(0x85); emit_byte(0xC0); // test rax, rax
        emit_byte(0x0F); emit_byte(0x85);                  // jnz rel32
        int off_pos = code_idx; emit_i32(0);
        int off = loop_start - (off_pos + 4);
        code_buffer[off_pos + 0] = (uint8_t)(off & 0xFF);
        code_buffer[off_pos + 1] = (uint8_t)((off >> 8) & 0xFF);
        code_buffer[off_pos + 2] = (uint8_t)((off >> 16) & 0xFF);
        code_buffer[off_pos + 3] = (uint8_t)((off >> 24) & 0xFF);
        return;
    }

    // ── for (init; cond; post) block ──────
    if (peek() == T_FOR) {
        consume();
        expect(T_LPAREN);

        // init
        for_init();
        expect(T_SEMICOLON);

        int cond_start = code_idx;          // adres początku warunku

        // cond (pusty = zawsze true)
        int jz = -1;
        if (peek() != T_SEMICOLON) {
            expr();
            jz = emit_jz();
        }
        expect(T_SEMICOLON);

        // post — musimy je pominąć przy pierwszym przejściu, a wykonać po ciele.
        // Technika: skocz przez post do ciała, po ciele wróć do post, po post do cond.
        int jmp_to_body = emit_jmp();       // skocz przez post do ciała
        int post_start = code_idx;         // adres początku post

        for_post();
        expect(T_RPAREN);

        emit_jmp_back(cond_start);          // po post → wróć do warunku
        patch_jmp(jmp_to_body);             // łataj skok do ciała

        block();

        emit_jmp_back(post_start);          // po ciele → skocz do post
        if (jz >= 0) patch_jmp(jz);        // łataj skok wyjścia z pętli
        return;
    }

    // if (expr) block [else block]
    if (peek() == T_IF) {
        consume();
        expect(T_LPAREN);
        if (peek() == T_RPAREN) {
            add_error(peek_line(), "Pusty warunek w if()");
            consume(); block();
            if (peek() == T_ELSE) { consume(); block(); }
            return;
        }
        expr();
        expect(T_RPAREN);
        int jz = emit_jz();
        block();
        if (peek() == T_ELSE) {
            consume();
            int jmp = emit_jmp();
            patch_jmp(jz);
            block();
            patch_jmp(jmp);
        }
        else {
            patch_jmp(jz);
        }
        return;
    }

    // else bez if
    if (peek() == T_ELSE) {
        add_error(peek_line(), "'else' bez poprzedzajacego 'if'");
        consume(); block(); return;
    }

    add_error_tok(peek_line(), "Nieoczekiwany token: ", token_name(peek()));
    consume();
}

// ─────────────────────────────────────────
//  ENTRY POINT
// ─────────────────────────────────────────

void parse_and_compile() {
    curr = 0; code_idx = 0;
    explicit_return_seen = false;
    vars_reset(); errors_reset();

    emit_push_rbp();
    emit_mov_rbp_rsp();
    int patch = code_idx;
    emit_sub_rsp(0);            // rozmiar ramki — zostanie załatany poniżej

    while (peek() != T_EOF) statement();

    // Bez jawnego `return` JIT musi mimo wszystko zakończyć funkcję poprawnym
    // epilogiem. W przeciwnym razie wykonanie spadnie za koniec bufora kodu.
    emit_mov_rax_imm64(0);
    emit_mov_rsp_rbp();
    emit_pop_rbp();
    emit_ret();

    // Dopełnij ramkę do wielokrotności 16 (wymóg ABI)
    int frame = stack_size;
    if (frame % 16 != 0) frame += 16 - (frame % 16);
    if (frame == 0)       frame = 16;

    int p = patch + 3;
    code_buffer[p + 0] = (uint8_t)(frame & 0xFF);
    code_buffer[p + 1] = (uint8_t)((frame >> 8) & 0xFF);
    code_buffer[p + 2] = (uint8_t)((frame >> 16) & 0xFF);
    code_buffer[p + 3] = (uint8_t)((frame >> 24) & 0xFF);
}

// ─────────────────────────────────────────
//  EXECUTE
// ─────────────────────────────────────────

typedef int (*JitFn)();

int execute_compiled() {
    if (code_idx == 0 || error_count > 0) return 0;
    // UWAGA: malloc() musi zwrócić pamięć z prawem do wykonania (PROT_EXEC).
    // W jądrze OS pamięć przydzielona przez malloc może już takie prawo mieć —
    // jeśli nie, należy użyć mmap(PROT_READ|PROT_WRITE|PROT_EXEC) lub mprotect().
    uint8_t* mem = (uint8_t*)malloc(code_idx);
    for (int i = 0; i < code_idx; i++) mem[i] = code_buffer[i];
    int result = ((JitFn)mem)();
    free(mem);
    return result;
}
