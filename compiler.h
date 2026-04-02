#pragma once
#include "types.h"

void lex(const char* src);
void parse_and_compile();
int  execute_compiled();
extern int code_idx;
bool compile_has_explicit_return();

int  compile_error_count();
void compile_get_error(int idx, int* out_line, const char** out_msg);
