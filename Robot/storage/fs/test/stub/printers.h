// Host-test stub for TimbreOS printers.h: only what fs.c uses.
#ifndef STUB_PRINTERS_H
#define STUB_PRINTERS_H
void print(const char *s);
void printCr(void);
void printDec(unsigned int n);
void printChar(unsigned char c);
void printHex2(unsigned int n);
void printHex4(unsigned int n);
void printHex(unsigned int n);
void printnHex(unsigned int n, unsigned int hex);
#endif
