#pragma once
// STB u¿ywa memcpy, memset, strlen
extern "C" void* memcpy(void* dst, const void* src, unsigned long n);
extern "C" void* memset(void* dst, int val, unsigned long n);
extern "C" unsigned long strlen(const char* s);