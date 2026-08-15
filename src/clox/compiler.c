#include "compiler.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "table.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // strtod
#include <string.h>

typedef struct {
    Token current;
    Token previous;
    Token next;
    bool hasNext;
    bool hadError;
    bool panicMode;
    bool silentMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_COMMA,      // ,
    PREC_ASSIGNMENT, // =
    PREC_TERNARY,    // ?:
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_LAMBDA,     // fun
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;

    // controls whether the closure captures a local variable or an upvalue from
    // the immediately surrounding function
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_CLASS_METHOD,
    TYPE_PROPERTY_GETTER,
    TYPE_PROPERTY_SETTER,
    TYPE_LAMBDA,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler Compiler;

struct Compiler {
    Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;
    Table identifiers;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
};

typedef struct Loop Loop;

struct Loop {
    uint32_t loopStart;
    uint32_t breakJumps[UINT8_COUNT];
    uint32_t breakCount;
    int scopeDepth;
    int enclosingBreakType;
    int loopVarIndex;
    int perIterLoopVarIndex;
    Loop *prev;
};

typedef struct Switch Switch;

struct Switch {
    uint32_t breakJumps[UINT8_COUNT];
    uint32_t breakCount;
    int scopeDepth;
    int localCount;
    int enclosingBreakType;
    Switch *prev;
};

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    bool hasSuperclass;
} ClassCompiler;

// Forward declaration.
static void declaration();
static void funDeclaration();
static void classDeclaration();
static void varDeclaration();
static void statement();
static void printStatement();
static void expressionStatement();
static void block();
static void ifStatement();
static void whileStatement();
static void forStatement();
static void switchStatement();
static void breakStatement();
static void continueStatement();
static void returnStatement();
static void function(FunctionType type);
static void method();
static void classMethod();
static void property();

static void parsePrecedence(Precedence precedence);
static void expression();
static void comma(bool canAssign);
static void ternary(bool canAssign);
static void logicOr(bool canAssign);
static void logicAnd(bool canAssign);
static void binary(bool canAssign);
static void grouping(bool canAssign);
static void unary(bool canAssign);
static void call(bool canAssign);
static void dot(bool canAssign);
static void lambda(bool canAssign);
static void number(bool canAssign);
static void string(bool canAssign);
static void literal(bool canAssign);
static void this_(bool canAssign);  // NOLINT(readability-identifier-naming)
static void super_(bool canAssign); // NOLINT(readability-identifier-naming)
static void variable(bool canAssign);

static void beginScope();
static void endScope();

static uint32_t identifierConstant(Token *name);
static uint32_t parseVariable(const char *errorMessage);

static Token syntheticToken(const char *text);

static void synchronize();
static ParseRule *getRule(TokenType type);

static Parser parser;
static Compiler *current = NULL;
static Loop *currentLoop = NULL;
static Switch *currentSwitch = NULL;
static ClassCompiler *currentClass = NULL;

#define BREAK_LOOP 1
#define BREAK_SWITCH 2

static int breakType = BREAK_LOOP;

static ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, comma, PREC_COMMA},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_QUESTION] = {NULL, ternary, PREC_TERNARY},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, logicAnd, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {lambda, NULL, PREC_LAMBDA},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, logicOr, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static Chunk *currentChunk() { return &current->function->chunk; }

static void initCompiler(Compiler *compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    initTable(&compiler->identifiers);
    current = compiler;

    switch (type) {
    case TYPE_FUNCTION:
    case TYPE_INITIALIZER:
    case TYPE_METHOD:
    case TYPE_CLASS_METHOD:
        current->function->name =
            copyString(parser.previous.start, parser.previous.length);
        break;
    case TYPE_LAMBDA:
        current->function->isLambda = true;
        break;
    default:
        break;
    }

    /*
     * In `interpret`, we push the function object being called at stack slot
     * zero. The compiler implicitly claims stack slot zero for the VM's own
     * internal use. We give it an empty name so that the user can’t write an
     * identifier that refers to it.
     */
    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    switch (type) {
    case TYPE_INITIALIZER:
    case TYPE_METHOD:
    case TYPE_PROPERTY_GETTER:
    case TYPE_PROPERTY_SETTER:
        local->name.start = "this";
        local->name.length = 4;
        break;
    default:
        local->name.start = "";
        local->name.length = 0;
        break;
    }
}

static void errorAt(Token *token, const char *message) {
    if (parser.panicMode) {
        return;
    }
    parser.panicMode = true;
    if (parser.silentMode) {
        parser.hadError = true;
        return;
    }
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void errorAtCurrent(const char *message) {
    errorAt(&parser.current, message);
}

static void error(const char *message) { errorAt(&parser.previous, message); }

static void errorFormat(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char message[128];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    error(message);
}

static void advance() {
    parser.previous = parser.current;
    if (parser.hasNext) {
        parser.current = parser.next;
        parser.hasNext = false;
        return;
    }

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) {
            break;
        }

        errorAtCurrent(parser.current.start);
    }
}

static void next() {
    if (parser.hasNext) {
        return;
    }

    for (;;) {
        parser.next = scanToken();
        parser.hasNext = true;
        if (parser.next.type != TOKEN_ERROR) {
            break;
        }

        errorAt(&parser.next, parser.next.start);
    }
}

static void consume(TokenType type, const char *message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool checkNext(TokenType type) {
    if (parser.hasNext) {
        return parser.next.type == type;
    }
    next();
    return parser.next.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

void beginScope() { current->scopeDepth++; }

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }
    Local *local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void markInitialized() {
    if (current->scopeDepth == 0) {
        return;
    }
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) {
        return false;
    }
    return memcmp(a->start, b->start, a->length) == 0;
}

static void declareVariable() {
    if (current->scopeDepth == 0) {
        return;
    }

    Token *name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            errorFormat("Already a variable named '%.*s' in this scope.",
                        name->length, name->start);
        }
    }

    addLocal(*name);
}

static int resolveLocal(Compiler *compiler, Token *name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                errorFormat("Accessing a local variable '%.*s' that has not "
                            "been initialized or assigned to.",
                            name->length, name->start);
            }
            /*
             * The switching value is still on the stack if we are
             * inside a switch statement.
             * Need to offset the return value by the switch depth
             * for each enclosing switch that this local was declared inside.
             * Locals declared before the switch keep their original index.
             */
            int switchDepth = 0;
            Switch *s = currentSwitch;
            while (s != NULL) {
                if (i >= s->localCount) {
                    switchDepth++;
                }
                s = s->prev;
            }
            return i + switchDepth;
        }
    }

    return -1;
}

static uint32_t addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
    uint32_t upvalueCount = compiler->function->upvalueCount;

    for (uint32_t i = 0; i < upvalueCount; i++) {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token *name) {
    if (compiler->enclosing == NULL) {
        return -1;
    }

    /*
     * A function captures - either a local or upvalue - only from the
     * immediately surrounding function, which is guaranteed to still be around
     * at the point that the inner function declaration executes.
     */
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return (int)addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return (int)addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void beginLoop(Loop *loop, uint32_t loopStart) {
    loop->loopStart = loopStart;
    loop->breakCount = 0;
    loop->prev = currentLoop;
    loop->scopeDepth = current->scopeDepth;
    loop->enclosingBreakType = breakType;
    loop->loopVarIndex = -1;
    loop->perIterLoopVarIndex = -1;
    currentLoop = loop;

    breakType = BREAK_LOOP;
}

static void beginSwitch(Switch *s) {
    s->breakCount = 0;
    s->prev = currentSwitch;
    s->scopeDepth = current->scopeDepth;
    s->localCount = current->localCount;
    s->enclosingBreakType = breakType;
    currentSwitch = s;

    breakType = BREAK_SWITCH;
}

#pragma region Emit Byte

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitLong(uint32_t value) {
    emitByte((uint8_t)(value >> 24) & UINT8_MAX);
    emitByte((uint8_t)(value >> 16) & UINT8_MAX);
    emitByte((uint8_t)(value >> 8) & UINT8_MAX);
    emitByte((uint8_t)value & UINT8_MAX);
}

static uint32_t emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(UINT8_MAX);
    emitByte(UINT8_MAX);
    return currentChunk()->count - 2;
}

static void emitLoop(uint32_t loopStart) {
    emitByte(OP_LOOP);

    uint32_t offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) {
        error("Loop body too large.");
    }

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static void patchJump(uint32_t offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    uint32_t jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & UINT8_MAX;
    currentChunk()->code[offset + 1] = jump & UINT8_MAX;
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0);
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static void emitConstant(Value value) {
    writeConstant(currentChunk(), value, parser.previous.line);
}

static void emitClosure(Value value) {
    writeClosure(currentChunk(), value, parser.previous.line);
}

static void defineVariable(uint32_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    if (global <= UINT8_MAX) {
        emitBytes(OP_DEFINE_GLOBAL, global);
    } else {
        emitByte(OP_DEFINE_GLOBAL_LONG);
        emitLong(global);
    }
}

static void namedVariable(Token *name, bool canAssign) {
    uint8_t getOp;
    uint8_t setOp;
    bool isLong = false;
    int64_t arg = resolveLocal(current, name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else {
        arg = resolveUpvalue(current, name);
        if (arg != -1) {
            getOp = OP_GET_UPVALUE;
            setOp = OP_SET_UPVALUE;
        } else {
            arg = (int64_t)identifierConstant(name);
            getOp = OP_GET_GLOBAL;
            setOp = OP_SET_GLOBAL;
            if (arg > UINT8_MAX) {
                isLong = true;
                getOp = OP_GET_GLOBAL_LONG;
                setOp = OP_SET_GLOBAL_LONG;
            }
        }
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        if (isLong) {
            emitByte(setOp);
            emitLong((uint32_t)arg);
        } else {
            emitBytes(setOp, (uint8_t)arg);
        }
    } else {
        if (isLong) {
            emitByte(getOp);
            emitLong((uint32_t)arg);
        } else {
            emitBytes(getOp, (uint8_t)arg);
        }
    }
}

void endScope() {
    current->scopeDepth--;

    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth >
               current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

static void endLoop(Loop *loop) {
    // Patch all `break` jumps.
    for (uint32_t i = 0; i < loop->breakCount; i++) {
        patchJump(loop->breakJumps[i]);
    }
    loop->breakCount = 0;
    currentLoop = loop->prev;
    breakType = loop->enclosingBreakType;
}

static void endSwitch(Switch *s) {
    // Patch all `break` jumps.
    for (uint32_t i = 0; i < s->breakCount; i++) {
        patchJump(s->breakJumps[i]);
    }
    s->breakCount = 0;
    currentSwitch = s->prev;
    breakType = s->enclosingBreakType;
}

#pragma endregion

static ObjFunction *endCompiler() {
    emitReturn();
    ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        if (function->name != NULL) {
            disassembleChunk(currentChunk(), function->name->chars);
        } else if (function->isLambda) {
            disassembleChunk(currentChunk(), "<lambda>");
        } else {
            disassembleChunk(currentChunk(), "<script>");
        }
    }
#endif

    freeTable(&current->identifiers);
    current = current->enclosing;
    return function;
}

static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            parsePrecedence(PREC_ASSIGNMENT);
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

#pragma region Statements

void declaration() {
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (check(TOKEN_FUN) && checkNext(TOKEN_IDENTIFIER)) {
        advance(); // consume 'fun'.
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) {
        synchronize();
    }
}

void funDeclaration() {
    uint32_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

void classDeclaration() {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token className = parser.previous;
    uint32_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    if (nameConstant > UINT8_MAX) {
        emitByte(OP_CLASS_LONG);
        emitLong(nameConstant);
    } else {
        emitBytes(OP_CLASS, (uint8_t)nameConstant);
    }
    // Define the variable before the body.
    // So that users can refer to the containing
    // class inside the bodies of its own methods.
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    classCompiler.hasSuperclass = false;
    currentClass = &classCompiler;

    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);

        if (identifiersEqual(&className, &parser.previous)) {
            // The class name is used in its own superclass clause.
            // Report it according to where the class name lives.
            if (resolveLocal(current, &className) != -1) {
                errorFormat("Accessing a local variable '%.*s' that has not "
                            "been initialized or assigned to.",
                            parser.previous.length, parser.previous.start);
            } else {
                errorFormat("Accessing a global variable '%.*s' that has not "
                            "been initialized or assigned to.",
                            parser.previous.length, parser.previous.start);
            }
        }

        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);

        namedVariable(&className, false);
        emitByte(OP_INHERIT);

        classCompiler.hasSuperclass = true;
    }

    /*
     * Right before compiling the class body, we call namedVariable(). That
     * helper function generates code to load a variable with the given name
     * onto the stack. Then we compile the methods.
     * This means that when we execute each OP_METHOD instruction, the stack has
     * the method’s closure on top with the class right under it.
     */
    namedVariable(&className, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (check(TOKEN_CLASS)) {
            advance(); // consume 'class'
            classMethod();
        } else if (check(TOKEN_IDENTIFIER) && checkNext(TOKEN_LEFT_BRACE)) {
            property();
        } else {
            method();
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    /*
     * Once we’ve reached the end of the methods, we no longer need the class
     * and tell the VM to pop it off the stack.
     */
    emitByte(OP_POP);

    if (classCompiler.hasSuperclass) {
        endScope();
    }

    currentClass = currentClass->enclosing;
}

void varDeclaration() {
    uint32_t global = parseVariable("Expect variable name.");

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_UNDEFINED);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_SWITCH)) {
        switchStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    uint32_t thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop the condition value from the stack if truthy.
    statement();

    uint32_t jump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP); // Pop the condition value from the stack if falsey.
    if (match(TOKEN_ELSE)) {
        statement();
    }
    patchJump(jump);
}

void whileStatement() {
    uint32_t loopStart = currentChunk()->count;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    uint32_t exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop the condition value from the stack if truthy.

    Loop loop;
    beginLoop(&loop, loopStart);
    statement(); // The loop body.
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP); // Pop the condition value from the stack if falsey.

    endLoop(&loop);
}

void forStatement() {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // Initializer.
    Token loopVarName;
    bool hasLoopVar = false;
    if (match(TOKEN_VAR)) {
        // Copy the loop var name on the stack.
        // It will be used  to create a new variable for each loop iteration.
        loopVarName = parser.current;
        hasLoopVar = true;
        varDeclaration();

        if (parser.hadError) {
            hasLoopVar = false;
        }
    } else if (!match(TOKEN_SEMICOLON)) {
        expressionStatement();
    }

    // The start point of the main loop body.
    uint32_t loopStart = currentChunk()->count;

    int64_t exitJump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP); // Pop the condition value from the stack if truthy.
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        // Skip evaluating the increment on the first enter.
        uint32_t bodyJump = emitJump(OP_JUMP);

        uint32_t incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP); // Pop the increment expression.
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        // Loop back to condition after evaluating the increment.
        emitLoop(loopStart);

        // The main loop body loops back to the increment if it exists.
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    Loop loop;
    beginLoop(&loop, loopStart);
    if (hasLoopVar) {
        // Manually create a new scope, then create a new variable for each loop
        // iteration in the scope.
        beginScope();
        loop.loopVarIndex = resolveLocal(current, &loopVarName);
        emitBytes(OP_GET_LOCAL, (uint8_t)loop.loopVarIndex);
        addLocal(loopVarName);
        markInitialized();
        loop.perIterLoopVarIndex = resolveLocal(current, &loopVarName);
    }

    statement(); // The main loop body.

    if (hasLoopVar) {
        // Write the per-iteration variable's value
        // back to the outer loop variable.
        emitBytes(OP_GET_LOCAL, (uint8_t)loop.perIterLoopVarIndex);
        emitBytes(OP_SET_LOCAL, (uint8_t)loop.loopVarIndex);
        emitByte(OP_POP);
        // End the scope where the loop variable lives in.
        endScope();
    }

    emitLoop(loopStart);
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP); // Pop the condition value from the stack if falsey.
    }

    endLoop(&loop);

    endScope();
}

void breakStatement() {
    if (currentLoop == NULL && currentSwitch == NULL) {
        error("Expect break in loop or switch body.");
        return;
    }

    if (breakType == BREAK_SWITCH) {
        // `break` a `switch` statement.
        if (currentSwitch->breakCount == UINT8_COUNT) {
            error("Too many break statements.");
            return;
        }

        // Pop all local variables declared in scopes deeper than the switch.
        int localCount = current->localCount;
        while (localCount > 0 && current->locals[localCount - 1].depth >
                                     currentSwitch->scopeDepth) {
            if (current->locals[localCount - 1].isCaptured) {
                emitByte(OP_CLOSE_UPVALUE);
            } else {
                emitByte(OP_POP);
            }
            localCount--;
        }
        currentSwitch->breakJumps[currentSwitch->breakCount] =
            emitJump(OP_JUMP);
        currentSwitch->breakCount++;
        consume(TOKEN_SEMICOLON, "Expect ';' after break.");
        return;
    }

    if (currentLoop->breakCount == UINT8_COUNT) {
        error("Too many break statements.");
        return;
    }

    // Pop all local variables declared in scopes deeper than the loop.
    int localCount = current->localCount;
    while (localCount > 0 &&
           current->locals[localCount - 1].depth > currentLoop->scopeDepth) {
        if (current->locals[localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        localCount--;
    }

    currentLoop->breakJumps[currentLoop->breakCount] = emitJump(OP_JUMP);
    currentLoop->breakCount++;
    consume(TOKEN_SEMICOLON, "Expect ';' after break.");
}

void continueStatement() {
    if (currentLoop == NULL) {
        error("Expect continue in loop body.");
        return;
    }

    if (currentLoop->loopVarIndex >= 0 &&
        currentLoop->perIterLoopVarIndex >= 0) {
        // Write the per-iteration variable's value
        // back to the outer loop variable.
        emitBytes(OP_GET_LOCAL, (uint8_t)currentLoop->perIterLoopVarIndex);
        emitBytes(OP_SET_LOCAL, (uint8_t)currentLoop->loopVarIndex);
        emitByte(OP_POP);
    }

    // Pop all local variables declared in scopes deeper than the loop.
    int localCount = current->localCount;
    while (localCount > 0 &&
           current->locals[localCount - 1].depth > currentLoop->scopeDepth) {
        if (current->locals[localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        localCount--;
    }

    emitLoop(currentLoop->loopStart);
    consume(TOKEN_SEMICOLON, "Expect ';' after continue.");
}

void returnStatement() {
    if (current->type == TYPE_SCRIPT) {
        error("Expect return in function body.");
    }
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        if (current->type == TYPE_INITIALIZER) {
            error("Can't return a value from an initializer.");
        }

        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

void switchStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after switch value.");

    consume(TOKEN_LEFT_BRACE, "Expect '{' before switch body.");

    Switch s;
    beginSwitch(&s);

    bool currentHadError = parser.hadError;
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (check(TOKEN_CASE)) {
            int64_t elseJump = -1;
            uint32_t caseJump[UINT8_MAX];
            uint32_t caseCount = 0;
            while (match(TOKEN_CASE) && !check(TOKEN_EOF)) {
                if (elseJump >= 0) {
                    patchJump((uint32_t)elseJump);
                    // Pop the previous check result if falsey.
                    emitByte(OP_POP);
                }
                expression();
                consume(TOKEN_COLON, "Expect ':' after case value.");

                // Check if the case value is equal to the switch value.
                emitByte(OP_CASE);

                // Jump to the next case if falsey.
                elseJump = emitJump(OP_JUMP_IF_FALSE);

                // Pop the check result if truthy.
                emitByte(OP_POP);

                // Skip all subsequent cases if truthy.
                if (caseCount == UINT8_COUNT) {
                    error("Too many case statements.");
                    break;
                }
                caseJump[caseCount] = emitJump(OP_JUMP);
                caseCount++;
            }
            if (parser.hadError && !currentHadError) {
                synchronize();
                parser.hadError = false;
                currentHadError = true;
            }

            // Jump to the statements if one of the cases if truthy.
            for (uint32_t i = 0; i < caseCount; i++) {
                patchJump(caseJump[i]);
            }

            // The case statements.
            while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
                   !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
                statement();
                if (parser.hadError && !currentHadError) {
                    synchronize();
                    parser.hadError = false;
                    currentHadError = true;
                }
            }

            // Skip the elseJump cleanup when a case matched and fell through.
            uint32_t skipCleanup = emitJump(OP_JUMP);

            // Skip all statements under the cases if all cases are falsey.
            if (elseJump >= 0) {
                patchJump((uint32_t)elseJump);
                // Pop the previous check result if falsey.
                emitByte(OP_POP);
            }

            patchJump(skipCleanup);
        } else if (check(TOKEN_DEFAULT)) {
            advance(); // Consume 'default'.
            consume(TOKEN_COLON, "Expect ':' after default.");
            while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
                statement();
                if (parser.hadError && !currentHadError) {
                    synchronize();
                    parser.hadError = false;
                    currentHadError = true;
                }
            }
        } else {
            advance();
            error("Expect switch case or default case.");
            synchronize();
        }
    }
    parser.hadError = currentHadError;

    endSwitch(&s);
    emitByte(OP_POP); // Pop the switch value.
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch body.");
}

void function(FunctionType type) {
    uint32_t line = parser.previous.line;
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    switch (type) {
    case TYPE_INITIALIZER:
    case TYPE_METHOD:
        consume(TOKEN_LEFT_PAREN, "Expect '(' after method name.");
        break;
    case TYPE_CLASS_METHOD:
        consume(TOKEN_LEFT_PAREN, "Expect '(' after class method name.");
        break;
    case TYPE_PROPERTY_GETTER:
    case TYPE_PROPERTY_SETTER:
        break;
    case TYPE_LAMBDA:
        consume(TOKEN_LEFT_PAREN, "Expect '(' after 'fun'.");
        break;
    default:
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
        break;
    }
    if (type == TYPE_PROPERTY_SETTER) {
        // Manually create a new parameter for the property setter.
        current->function->arity = 1;
        Token propertySetterVarName = {.type = TOKEN_IDENTIFIER,
                                       .start = "value",
                                       .length = 5,
                                       .line = line};
        addLocal(propertySetterVarName);
        markInitialized();
    } else if (type != TYPE_PROPERTY_GETTER && !check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }

    switch (type) {
    case TYPE_INITIALIZER:
    case TYPE_METHOD:
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after method parameters.");
        consume(TOKEN_LEFT_BRACE, "Expect '{' before method body.");
        break;
    case TYPE_CLASS_METHOD:
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after class method parameters.");
        consume(TOKEN_LEFT_BRACE, "Expect '{' before class method body.");
        break;
    case TYPE_PROPERTY_GETTER:
    case TYPE_PROPERTY_SETTER:
        consume(TOKEN_LEFT_BRACE, "Expect '{' after property accessor.");
        break;
    case TYPE_LAMBDA:
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after lambda parameters.");
        consume(TOKEN_LEFT_BRACE, "Expect '{' before lambda body.");
        break;
    default:
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters.");
        consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
        break;
    }

    block();

    ObjFunction *function = endCompiler();
    emitClosure(OBJ_VAL(function));

    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte((int)compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

void method() {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint32_t constant = identifierConstant(&parser.previous);

    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
        memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    function(type);

    if (constant > UINT8_MAX) {
        emitByte(OP_METHOD_LONG);
        emitLong(constant);
    } else {
        emitBytes(OP_METHOD, (uint8_t)constant);
    }
}

void classMethod() {
    consume(TOKEN_IDENTIFIER, "Expect class method name.");
    uint32_t constant = identifierConstant(&parser.previous);

    function(TYPE_CLASS_METHOD);

    if (constant > UINT8_MAX) {
        emitByte(OP_CLASS_METHOD_LONG);
        emitLong(constant);
    } else {
        emitBytes(OP_CLASS_METHOD, (uint8_t)constant);
    }
}

void property() {
    uint32_t constant = identifierConstant(&parser.current);
    advance(); // consume property name
    advance(); // consume '{'

    uint8_t hasGetter = 0;
    uint8_t hasSetter = 0;
    uint8_t isGetterFirst = 0;
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (parser.panicMode) {
            break;
        }
        if (check(TOKEN_GET)) {
            advance(); // consume 'get'
            if (hasGetter) {
                error("Property accessor already defined.");
            }
            function(TYPE_PROPERTY_GETTER);
            if (!parser.panicMode) {
                hasGetter = 1;
                if (!hasSetter) {
                    isGetterFirst = 1;
                }
            }
        } else if (check(TOKEN_SET)) {
            advance(); // consume 'set'
            if (hasSetter) {
                error("Property accessor already defined.");
            }
            function(TYPE_PROPERTY_SETTER);
            if (!parser.panicMode) {
                hasSetter = 1;
            }
        } else {
            error("A get or set accessor expected.");
            break;
        }
    }

    if (parser.panicMode) {
        return;
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after property statement.");

    if (constant > UINT8_MAX) {
        emitByte(OP_PROPERTY_LONG);
        emitLong(constant);
    } else {
        emitBytes(OP_PROPERTY, (uint8_t)constant);
    }

    uint8_t byte = (hasGetter << 2) | (hasSetter << 1) | isGetterFirst;
    emitByte(byte);
}

#pragma endregion

#pragma region Expressions

void literal(bool canAssign) {
    switch (parser.previous.type) {
    case TOKEN_FALSE:
        emitByte(OP_FALSE);
        break;
    case TOKEN_NIL:
        emitByte(OP_NIL);
        break;
    case TOKEN_TRUE:
        emitByte(OP_TRUE);
        break;
    default:
        return; // Unreachable.
    }
}

void string(bool canAssign) {
    // Remove the starting " and closing ".
    emitConstant(OBJ_VAL(copyStringNormalized(parser.previous.start + 1,
                                              parser.previous.length - 2)));
}

void this_(bool canAssign) {
    if (currentClass == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

void super_(bool canAssign) {
    if (currentClass == NULL) {
        error("Can't use 'super' outside of a class.");
    } else if (!currentClass->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint32_t name = identifierConstant(&parser.previous);

    Token thisToken = syntheticToken("this");
    Token superToken = syntheticToken("super");
    namedVariable(&thisToken, false);

    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(&superToken, false);
        if (name > UINT8_MAX) {
            emitByte(OP_SUPER_INVOKE_LONG);
            emitLong(name);
        } else {
            emitBytes(OP_SUPER_INVOKE, (uint8_t)name);
        }
        emitByte(argCount);
    } else {
        namedVariable(&superToken, false);
        if (name > UINT8_MAX) {
            emitByte(OP_GET_SUPER_LONG);
            emitLong(name);
        } else {
            emitBytes(OP_GET_SUPER, (uint8_t)name);
        }
    }
}

void variable(bool canAssign) { namedVariable(&parser.previous, canAssign); }

void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

void lambda(bool canAssign) { function(TYPE_LAMBDA); }

void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint32_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        if (name > UINT8_MAX) {
            emitByte(OP_SET_PROPERTY_LONG);
            emitLong(name);
        } else {
            emitBytes(OP_SET_PROPERTY, (uint8_t)name);
        }
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        if (name > UINT8_MAX) {
            emitByte(OP_INVOKE_LONG);
            emitLong(name);
            emitByte(argCount);
        } else {
            emitBytes(OP_INVOKE, (uint8_t)name);
            emitByte(argCount);
        }
    } else {
        if (name > UINT8_MAX) {
            emitByte(OP_GET_PROPERTY_LONG);
            emitLong(name);
        } else {
            emitBytes(OP_GET_PROPERTY, (uint8_t)name);
        }
    }
}

void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType) {
    case TOKEN_BANG:
        emitByte(OP_NOT);
        break;
    case TOKEN_MINUS:
        emitByte(OP_NEGATE);
        break;
    default:
        return; // Unreachable.
    }
}

void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule *rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
    case TOKEN_BANG_EQUAL:
        emitBytes(OP_EQUAL, OP_NOT);
        break;
    case TOKEN_EQUAL_EQUAL:
        emitByte(OP_EQUAL);
        break;
    case TOKEN_GREATER:
        emitByte(OP_GREATER);
        break;
    case TOKEN_GREATER_EQUAL:
        emitByte(OP_GREATER_EQUAL);
        break;
    case TOKEN_LESS:
        emitByte(OP_LESS);
        break;
    case TOKEN_LESS_EQUAL:
        emitByte(OP_LESS_EQUAL);
        break;
    case TOKEN_PLUS:
        emitByte(OP_ADD);
        break;
    case TOKEN_MINUS:
        emitByte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        emitByte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        emitByte(OP_DIVIDE);
        break;
    default:
        return; // Unreachable.
    }
}

void logicAnd(bool canAssign) {
    uint32_t endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop the left operand from the stack.
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

void logicOr(bool canAssign) {
    uint32_t elseJump = emitJump(OP_JUMP_IF_FALSE);
    uint32_t endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP); // Pop the left operand from the stack.

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

void ternary(bool canAssign) {
    uint32_t testJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Pop the `test` value from the stack if truthy.

    // Parse expression for the truthy branch. (`consequent` in cslox.)
    parsePrecedence(PREC_TERNARY);
    uint32_t consequentJump = emitJump(OP_JUMP);
    patchJump(testJump);

    consume(TOKEN_COLON, "Expect ':' after '?'.");
    emitByte(OP_POP); // Pop the `test` value from the stack if falsey.
    // Parse expression for the falsey branch. (`alternate` in cslox.)
    parsePrecedence(PREC_TERNARY);
    patchJump(consequentJump);
}

void comma(bool canAssign) {
    // Pop the left expression.
    emitByte(OP_POP);
    // Parse the right expression.
    parsePrecedence(PREC_ASSIGNMENT);
}

void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Unexpected expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

void expression() { parsePrecedence(PREC_COMMA); }

#pragma endregion

uint32_t identifierConstant(Token *name) {
    ObjString *varName = copyString(name->start, name->length);
    Value indexValue;
    if (tableGet(&current->identifiers, varName, &indexValue)) {
        return (uint32_t)AS_NUMBER(indexValue);
    }
    uint32_t index = addConstant(currentChunk(), OBJ_VAL(varName));
    tableSet(&current->identifiers, varName, NUMBER_VAL((double)index));
    return index;
}

static uint32_t parseVariable(const char *errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) {
        return 0;
    }

    return identifierConstant(&parser.previous);
}

Token syntheticToken(const char *text) {
    Token token;
    token.start = text;
    token.length = (uint32_t)strlen(text);
    return token;
}

void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) {
            return;
        }
        switch (parser.current.type) {
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_BREAK:
        case TOKEN_CONTINUE:
        case TOKEN_SWITCH:
        case TOKEN_RETURN:
            return;

        default:; // Do nothing.
        }

        advance();
    }
}

ParseRule *getRule(TokenType type) { return &rules[type]; }

ObjFunction *compile(const char *source, bool isREPL) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hasNext = false;
    parser.hadError = false;
    parser.panicMode = false;
    parser.silentMode = isREPL;

    advance();
    if (isREPL) {
        expression();
        consume(TOKEN_EOF, "");
        if (!parser.hadError) {
            ObjFunction *func = endCompiler();
            return func;
        }
        // Resume normal state.
        freeTable(&compiler.identifiers);
        freeFunction(compiler.function);
        current = NULL;
        initCompiler(&compiler, TYPE_SCRIPT);
        initScanner(source);
        parser.hasNext = false;
        parser.hadError = false;
        parser.panicMode = false;
        parser.silentMode = false;
        advance();
    }

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction *function = endCompiler();
    return (int)parser.hadError ? NULL : function;
}

void markCompilerRoots() {
    Compiler *compiler = current;
    while (compiler != NULL) {
        markObject((Obj *)compiler->function);
        compiler = compiler->enclosing;
    }
}
