namespace cslox
{
    class Parser
    {
        private class ParseError : Exception {}

        private readonly List<Token> tokens;
        private int current = 0;

        internal Parser(List<Token> tokens)
        {
            this.tokens = tokens;
        }

        // program        → declaration* EOF ;
        internal List<Stmt> Parse()
        {
            List<Stmt> statements = [];

            while (!IsAtEnd())
            {
                var stmt = Declaration();
                if (stmt == null)
                {
                    continue;
                }
                statements.Add(stmt);
            }

            return statements;
        }

        // declaration    → varDecl
        //                | statement ;
        private Stmt? Declaration()
        {
            try
            {
                if (Match(TokenType.VAR)) return VarDeclaration();

                return Statement();
            }
            catch (ParseError)
            {
                Synchronize();
                return null;
            }
        }

        // statement      → exprStmt
        //                | printStmt
        //                | block ;
        private Stmt Statement()
        {
            if (Match(TokenType.PRINT)) return PrintStmt();
            if (Match(TokenType.LEFT_BRACE)) return Block();

            return ExprStmt();
        }

        // block          → "{" declaration* "}" ;
        private Stmt.Block Block()
        {
            List<Stmt> statements = [];

            while (!Check(TokenType.RIGHT_BRACE) && !IsAtEnd())
            {
                var stmt = Declaration();
                if (stmt == null)
                {
                    continue;
                }
                statements.Add(stmt);
            }

            Consume(TokenType.RIGHT_BRACE, "Expect '}' after block.");
            return new Stmt.Block(statements);
        }

        // varDecl        → "var" IDENTIFIER ( "=" expression )? ";" ;
        private Stmt.Var VarDeclaration()
        {
            var name = Consume(TokenType.IDENTIFIER, "Expect variable name.");

            Expr? initializer = null;
            if (Match(TokenType.EQUAL))
            {
                initializer = Expression();
            }

            Consume(TokenType.SEMICOLON, "Expect ';' after variable declaration.");
            return new Stmt.Var(name, initializer);
        }

        // exprStmt       → expression ";" ;
        private Stmt.Expression ExprStmt()
        {
            var expr = Expression();
            Consume(TokenType.SEMICOLON, "Expect ';' after expression.");
            return new Stmt.Expression(expr);
        }

        // printStmt      → "print" expression ";" ;
        private Stmt.Print PrintStmt()
        {
            var value = Expression();
            Consume(TokenType.SEMICOLON, "Expect ';' after value.");
            return new Stmt.Print(value);
        }

        // expression     → comma ;
        private Expr Expression()
        {
            return Comma();
        }

        // comma          → assignment ( ( "," ) assignment )* ;
        private Expr Comma()
        {
            var expr = Assignment();

            while (Match(TokenType.COMMA))
            {
                var @operator = Previous();
                var right = Assignment();
                expr = new Expr.Binary(expr, @operator, right);
            }

            return expr;
        }

        // assignment     → IDENTIFIER "=" assignment
        //                | ternary ;
        private Expr Assignment()
        {
            var expr = Ternary();

            if (Match(TokenType.EQUAL))
            {
                var equals = Previous();
                var value = Assignment();

                if (expr is Expr.Variable exprVar)
                {
                    return new Expr.Assign(exprVar.name, value);
                }

                Error(equals, "Invalid assignment target.");
            }

            return expr;
        }

        // ternary        → equality ( "?" ternary ":" ternary )? ;
        private Expr Ternary()
        {
            var expr = Equality();

            if (Match(TokenType.QUESTION))
            {
                var consequent = Ternary();
                Consume(TokenType.COLON, "Expect ':' after '?'.");
                var alternate = Ternary();
                return new Expr.Ternary(expr, consequent, alternate);
            }

            return expr;
        }

        // equality       → comparison ( ( "!=" | "==" ) comparison )* ;
        private Expr Equality()
        {
            var expr = Comparison();

            while (Match(TokenType.BANG_EQUAL, TokenType.EQUAL_EQUAL))
            {
                var @operator = Previous();
                var right = Comparison();
                expr = new Expr.Binary(expr, @operator, right);
            }

            return expr;
        }

        // comparison     → term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
        private Expr Comparison()
        {
            var expr = Term();

            while (Match(TokenType.GREATER, TokenType.GREATER_EQUAL, TokenType.LESS, TokenType.LESS_EQUAL))
            {
                var @operator = Previous();
                var right = Term();
                expr = new Expr.Binary(expr, @operator, right);
            }

            return expr;
        }

        // term           → factor ( ( "-" | "+" ) factor )* ;
        private Expr Term()
        {
            var expr = Factor();

            while (Match(TokenType.MINUS, TokenType.PLUS))
            {
                var @operator = Previous();
                var right = Factor();
                expr = new Expr.Binary(expr, @operator, right);
            }

            return expr;
        }

        // factor         → unary ( ( "/" | "*" ) unary )* ;
        private Expr Factor()
        {
            var expr = Unary();

            while (Match(TokenType.SLASH, TokenType.STAR))
            {
                var @operator = Previous();
                var right = Unary();
                expr = new Expr.Binary(expr, @operator, right);
            }

            return expr;
        }

        // unary          → ( "!" | "-" ) unary
        //                | primary ;
        private Expr Unary()
        {
            if (Match(TokenType.BANG, TokenType.MINUS))
            {
                var @operator = Previous();
                var right = Unary();
                return new Expr.Unary(@operator, right);
            }

            return Primary();
        }

        // primary        → NUMBER | STRING | "true" | "false" | "nil"
        //                | "(" expression ")"
        //                | IDENTIFIER ;
        private Expr Primary()
        {
            // Binary operators.
            if (Match(
                TokenType.COMMA,
                TokenType.BANG_EQUAL,
                TokenType.EQUAL_EQUAL,
                TokenType.GREATER,
                TokenType.GREATER_EQUAL,
                TokenType.LESS,
                TokenType.LESS_EQUAL,
                TokenType.MINUS,
                TokenType.PLUS,
                TokenType.SLASH,
                TokenType.STAR
            ))
            {
                var @operator = Previous();

                // Parse and discard a right-hand operand
                // with the appropriate precedence
                switch (@operator.type)
                {
                    case TokenType.COMMA:
                        Ternary();
                        break;
                    case TokenType.BANG_EQUAL:
                    case TokenType.EQUAL_EQUAL:
                        Comparison();
                        break;
                    case TokenType.GREATER:
                    case TokenType.GREATER_EQUAL:
                    case TokenType.LESS:
                    case TokenType.LESS_EQUAL:
                        Term();
                        break;
                    case TokenType.MINUS:
                    case TokenType.PLUS:
                        Factor();
                        break;
                    case TokenType.SLASH:
                    case TokenType.STAR:
                        Unary();
                        break;
                }

                // Report error.
                throw Error(@operator, "Binary operator without a left-hand operand.");
            }

            if (Match(TokenType.FALSE)) return new Expr.Literal(false);
            if (Match(TokenType.TRUE)) return new Expr.Literal(true);
            if (Match(TokenType.NIL)) return new Expr.Literal(null);

            if (Match(TokenType.NUMBER, TokenType.STRING))
            {
                return new Expr.Literal(Previous().literal);
            }

            if (Match(TokenType.IDENTIFIER))
            {
                return new Expr.Variable(Previous());
            }

            if (Match(TokenType.LEFT_PAREN))
            {
                var expr = Expression();
                Consume(TokenType.RIGHT_PAREN, "Expect ')' after expression.");
                return expr;
            }

            throw Error(Peek(), "Unexpected expression.");
        }

        private bool Match(params TokenType[] types)
        {
            foreach (var type in types)
            {
                if (Check(type))
                {
                    Advance();
                    return true;
                }
            }

            return false;
        }

        private bool Check(TokenType type)
        {
            if (IsAtEnd())
            {
                return false;
            }
            return Peek().type == type;
        }

        private Token Advance()
        {
            if (!IsAtEnd())
            {
                current++;
            }
            return Previous();
        }

        private bool IsAtEnd()
        {
            return Peek().type == TokenType.EOF;
        }

        private Token Peek()
        {
            return tokens[current];
        }

        private Token Previous()
        {
            return tokens[current - 1];
        }

        private Token Consume(TokenType type, string message)
        {
            if (Check(type)) return Advance();

            throw Error(Peek(), message);
        }

        static private ParseError Error(Token token, string message)
        {
            Lox.Error(token, message);
            return new ParseError();
        }

        // 'Synchronize' discards tokens until it thinks it has found a statement boundary.
        private void Synchronize()
        {
            Advance();

            while (!IsAtEnd())
            {
                if (Previous().type == TokenType.SEMICOLON) return;

                switch(Peek().type)
                {
                    case TokenType.CLASS:
                    case TokenType.FUN:
                    case TokenType.VAR:
                    case TokenType.FOR:
                    case TokenType.IF:
                    case TokenType.WHILE:
                    case TokenType.PRINT:
                    case TokenType.RETURN:
                        return;
                }

                Advance();
            }
        }
    }
}
