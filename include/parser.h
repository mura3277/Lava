#ifndef LAVA_PARSER_H
#define LAVA_PARSER_H

#include <stdlib.h>
#include "token.h"
#include "lexer.h"
#include "ast.h"

static int TOKENS_CONSUMED = 0;

typedef struct Parser {
    Token* token;
    Lexer* lexer;
    TokenType type;
} Parser;

Parser* parserInit(Lexer* lexer) {
    Parser* parser = RALLOC(1, sizeof(Parser));
    parser->lexer = lexer;
    parser->token = lexNextToken(lexer);
    parser->type = parser->token->type;
    return parser;
}

#undef ERROR //Redefine ERROR now that Lexing has finished
#define ERROR(MSG, ...) \
size_t start = findStartOfErrorSnippet(parser->lexer); \
printf("\n%s:%zu:%zu: ", parser->lexer->filepath, parser->lexer->line, parser->lexer->pos - parser->lexer->col); \
LAVA(MSG, "Internal Error: ", __VA_ARGS__) \
printSyntaxErrorLocation(parser->lexer, start); \
exit(EXIT_FAILURE); \

Token* parserConsume(Parser* parser, TokenType type) {
    ASSERT(parser->token->type != type, "Unexpected token! expected: %s, got: %s (%s)", TOKEN_NAMES[type], TOKEN_NAMES[parser->token->type], viewToStr(&parser->token->view));
    INFO("Consumed: %s: %s", TOKEN_NAMES[parser->token->type], viewToStr(&parser->token->view));
    TOKENS_CONSUMED++;
    Token* prev = parser->token;
    parser->token = lexNextToken(parser->lexer);
    parser->type = parser->token->type;
    if (parser->token->type == TOKEN_UNEXPECTED) {
        ERROR("Unexpected Token! (%s)", viewToStr(&parser->token->view));
    }
    return prev;
}

//TODO, pass the return of parserConsume directly
AST* parseIdentifier(Parser* parser, Scope* scope) {
    Token* identifier = parser->token;
    parserConsume(parser, TOKEN_IDENTIFIER);
    return initAST(identifier, AST_ID, 0);
}

AST* parseDataType(Parser* parser, Scope* scope) {
    Token* varType = parser->token;
    parserConsume(parser, parser->type); //Type was checked by parseTokens, so this should be valid. parserConsume will panic if not.
    return initAST(varType, AST_TYPE, 0);
}

void parseCStatement(DynArray* nodes, Parser* parser, Scope* scope) {
    Token* statement = parser->token;
    parserConsume(parser, TOKEN_C_STATEMENT);
    arrayAppend(nodes, initAST(statement, AST_C, 0));
}

AST* parseFactor(Parser* parser, Scope* scope);
AST* parseExpression(Parser* parser, Scope* scope);
void parseDefinition(DynArray* nodes, Parser* parser, Scope* scope);
AST* parseAST(Parser* parser, Scope* scope, TokenType breakToken);

AST* parseFactor(Parser* parser, Scope* scope) {
    Token* token = parser->token;
    if (parser->token->flags & DATA_VALUE) {
        parserConsume(parser, parser->type);
        return initAST(token, AST_VALUE, 0);
    } else if (parser->type == TOKEN_LPAREN) {
        parserConsume(parser, TOKEN_LPAREN);
        AST* expression = parseExpression(parser, scope);
        parserConsume(parser, TOKEN_RPAREN);
        return expression;
    } else if (parser->type == TOKEN_LBRACKET) {
        //TODO parse array
    } else {
        return parseExpression(parser, scope);
    }
}

AST* parseTerm(Parser* parser, Scope* scope) {
    AST* factor = parseFactor(parser, scope);
    while (parser->type == TOKEN_DIVIDE || parser->type == TOKEN_MULTIPLY) {
        Token* token = parser->token;
        parserConsume(parser, parser->type);
        factor = initASTBinop(token, factor, parseFactor(parser, scope));
    }
    return factor;
}

AST* parseExpression(Parser* parser, Scope* scope) {
    AST* ast = parseTerm(parser, scope);

    if (parser->token->flags & TYPE_BINOP) {
        Token* token = parser->token;
        parserConsume(parser, parser->type);

        AST* right = parseTerm(parser, scope);
        return initASTBinop(token, ast, right);
    }
    return ast;
}

void parseReturn(DynArray* nodes, Parser* parser, Scope* scope) {
    parserConsume(parser, TOKEN_RETURN);
    AST* expression = parseExpression(parser, scope);
    parserConsume(parser, TOKEN_EOS);
    arrayAppend(nodes, initASTReturn(expression));
}

void parseImport(DynArray* nodes, Parser* parser, Scope* scope) {
    Token* token = parser->token;
    parserConsume(parser, TOKEN_IMPORT);
    parserConsume(parser, TOKEN_EOS);
    arrayAppend(nodes, initAST(token, AST_IMPORT, 0));
}

bool isValueCompatible(AST* dataType, AST* expression) {
    return true;
    if (dataType->token->flags & VAR_INT) {
        return expression->token->type == TOKEN_INTEGER_VALUE;
    } else if (dataType->token->flags & VAR_FLOAT) {
        return expression->token->type == TOKEN_FLOAT_VALUE;
    } else if (dataType->token->flags & VAR_STR) {
        return expression->token->type == TOKEN_STRING_VALUE;
    } else if (dataType->token->flags & VAR_CHAR) {
        return expression->token->type == TOKEN_CHAR_VALUE;
    } else if (dataType->token->flags & VAR_BOOL) {
        return expression->token->type == TOKEN_BOOLEAN_VALUE;
    } else {
        return false;
    }
}

void parseVarDefinition(DynArray* nodes, Parser* parser, Scope* scope, AST* dataType, AST* identifier) {
    AST* expression = NULL; //TODO somehow remove use of null
    if (parser->type == TOKEN_ASSIGNMENT) {
        parserConsume(parser, TOKEN_ASSIGNMENT);
        expression = parseExpression(parser, scope);
        if (!isValueCompatible(dataType, expression)) {
            ERROR("%s (%s) incompatible with: %s", TOKEN_NAMES[expression->token->type], viewToStr(&expression->token->view), TOKEN_NAMES[dataType->token->type]);
        }
    }
    arrayAppend(nodes, initASTVar(dataType, identifier, expression, 0));
}

void parseFuncDefinition(DynArray* nodes, Parser* parser, Scope* scope, AST* returnType, AST* identifier) {
    parserConsume(parser, TOKEN_LPAREN);
    DynArray* nodesArgs = arrayInit(sizeof(AST *));
    parseDefinition(nodesArgs, parser, scope);
    AST* arguments = initASTComp(nodesArgs);
    parserConsume(parser, TOKEN_RPAREN);
    parserConsume(parser, TOKEN_LBRACE);
    AST* statements = parseAST(parser, scope, TOKEN_RBRACE);
    parserConsume(parser, TOKEN_RBRACE);
    arrayAppend(nodes, initASTFunc(returnType, identifier, arguments, statements, 0));
}

void parseDefinition(DynArray* nodes, Parser* parser, Scope* scope) {
    AST* dataType = NULL;
    AST* identifier = NULL;
    while (parser->token->flags & DATA_TYPE || parser->type == TOKEN_IDENTIFIER) {
        if (parser->type == TOKEN_IDENTIFIER) {
            ASSERT(dataType == NULL, "Comma delimited var def did not start with valid dataType! (%d)", parser->token->type)
            //TODO: alloc new token as copy of dataType->token?
            dataType = initAST(dataType->token, AST_TYPE, 0);
        } else if (parser->token->flags & DATA_TYPE) {
            dataType = parseDataType(parser, scope);
        }
        identifier = parseIdentifier(parser, scope);
        if (parser->type == TOKEN_LPAREN) { //Parsing Function Def
            parseFuncDefinition(nodes, parser, scope, dataType, identifier);
            break; //Break out of loop, as no more work once function is constructed
        }
        parseVarDefinition(nodes, parser, scope, dataType, identifier);
        if (parser->type == TOKEN_EOS) { //Reached end of variable definitions
            parserConsume(parser, TOKEN_EOS);
            break;
        } else if (parser->type == TOKEN_COMMA) { //More variable definitions to come, continue
            parserConsume(parser, TOKEN_COMMA);
            continue;
        }
    }
}

void parseStructDefinition(DynArray* nodes, Parser* parser, Scope* scope) {
    parserConsume(parser, TOKEN_STRUCT);
    AST* identifier = parseIdentifier(parser, scope);
    parserConsume(parser, TOKEN_LBRACE);
    AST* members = parseAST(parser, scope, TOKEN_RBRACE);
    parserConsume(parser, TOKEN_RBRACE);
    arrayAppend(nodes, initASTStruct(identifier, members, 0));
}

void parseEnumDefinition(DynArray* nodes, Parser* parser, Scope* scope) {
    parserConsume(parser, TOKEN_ENUM);
    Token* flag = parser->type == TOKEN_FLAG ? parserConsume(parser, TOKEN_FLAG) : &TOKEN_NONE;
    AST* identifier = parseIdentifier(parser, scope);
    parserConsume(parser, TOKEN_LBRACE);
    DynArray* constants = arrayInit(sizeof(AST*));
    size_t constantValue = 0; //Initial enum constant value
    while (parser->type == TOKEN_IDENTIFIER) {
        AST* constant = parseIdentifier(parser, scope);
        if (parser->type == TOKEN_ASSIGNMENT) {
            parserConsume(parser, TOKEN_ASSIGNMENT);
            AST* expression = parseExpression(parser, scope);
            arrayAppend(constants, initASTAssign(constant, expression));
        } else {
            arrayAppend(constants, flag->type == TOKEN_FLAG ? initASTAssign(constant, initASTInteger(1 << constantValue)) : constant);
        }
        if (parser->type == TOKEN_COMMA) parserConsume(parser, TOKEN_COMMA);
        if (parser->type == TOKEN_RBRACE) break;
        constantValue++;
    }
    parserConsume(parser, TOKEN_RBRACE);
    arrayAppend(nodes, initASTEnum(identifier, initASTComp(constants), 0));
}

AST* parseAST(Parser* parser, Scope* scope, TokenType breakToken) {
    DynArray* nodes = arrayInit(sizeof(AST*));
    while (parser->type != breakToken) {
        if (parser->token->flags & DATA_TYPE) {
            parseDefinition(nodes, parser, scope);
        } else if (parser->type == TOKEN_STRUCT) {
            parseStructDefinition(nodes, parser, scope);
        } else if (parser->type == TOKEN_ENUM) {
            parseEnumDefinition(nodes, parser, scope);
        } else if (parser->type == TOKEN_C_STATEMENT) {
            parseCStatement(nodes, parser, scope);
        } else if (parser->type == TOKEN_RETURN) {
            parseReturn(nodes, parser, scope);
        } else if (parser->type == TOKEN_IMPORT) {
            parseImport(nodes, parser, scope);
        } else {
            ERROR("Token Was Not Consumed Or Parsed! %s (%s)", TOKEN_NAMES[parser->type], viewToStr(&parser->token->view));
        }
    }
    return initASTComp(nodes);
}
#endif