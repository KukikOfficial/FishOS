#pragma once
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
static inline int isalnum(int c) { return isdigit(c) || isalpha(c); }
static inline int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int isprint(int c) { return c >= 32 && c < 127; }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int tolower(int c) { return isupper(c) ? c + 32 : c; }
static inline int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
