#include "compiler.h"
#include "scanner.h"
#include <stdio.h>

void compile(const char *source) {
    initScanner(source);
    uint32_t line = 0;
    for (;;) {
        Token token = scanToken();
        if (token.line != line) {
            fprintf(stdout, "%4d ", token.line);
            line = token.line;
        } else {
            fprintf(stdout, "   | ");
        }
        fprintf(stdout, "%2d '%.*s'\n", token.type, token.length, token.start);

        if (token.type == TOKEN_EOF) {
            break;
        }
    }
}
