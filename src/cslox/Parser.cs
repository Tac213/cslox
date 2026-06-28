namespace cslox
{
    class Parser
    {
        internal class ParseError : Exception {}

        private readonly List<Token> tokens;
        private int current = 0;
        private int loopBodyDepth = 0;
        private int funBodyDepth = 0;

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

        // declaration    → funDecl
        //                | varDecl ;
        //                | statement ;
        private Stmt? Declaration()
        {
            try
            {
                if (Check(TokenType.FUN) && CheckNext(TokenType.IDENTIFIER))
                {
                    Advance(); // consume FUN
                    return Function("function");
                }
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
        //                | ifStmt
        //                | forStmt
        //                | printStmt
        //                | returnStmt
        //                | whileStmt
        //                | breakStmt
        //                | continueStmt
        //                | block ;
        private Stmt Statement()
        {
            if (Match(TokenType.IF)) return IfStmt();
            if (Match(TokenType.FOR)) return ForStmt();
            if (Match(TokenType.PRINT)) return PrintStmt();
            if (Match(TokenType.WHILE)) return WhileStmt();
            if (Match(TokenType.LEFT_BRACE)) return Block();

            if (Match(TokenType.RETURN))
            {
                if (funBodyDepth <= 0)
                {
                    throw Error(Peek(), "Expect return in function body.");
                }
                return ReturnStmt();
            }

            if (Match(TokenType.BREAK))
            {
                if (loopBodyDepth <= 0)
                {
                    throw Error(Peek(), "Expect break in loop body.");
                }
                return BreakStmt();
            }

            if (Match(TokenType.CONTINUE))
            {
                if (loopBodyDepth <= 0)
                {
                    throw Error(Peek(), "Expect continue in loop body.");
                }
                return ContinueStmt();
            }

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

        // funDecl        → "fun" function ;
        // function       → IDENTIFIER "(" parameters? ")" block ;
        // parameters     → IDENTIFIER ( "," IDENTIFIER )* ;
        private Stmt.Function Function(string kind) {
            Token name = Consume(TokenType.IDENTIFIER, $"Expect {kind} name.");
            Consume(TokenType.LEFT_PAREN, $"Expect '(' after {kind} name.");
            List<Token> parameters = [];
            if (!Check(TokenType.RIGHT_PAREN))
            {
                do
                {
                    if (parameters.Count >= 255)
                    {
                        Error(Peek(), "Can't have more than 255 parameters.");
                    }

                    parameters.Add(Consume(TokenType.IDENTIFIER, "Expect parameter name."));
                } while (Match(TokenType.COMMA));
            }
            Consume(TokenType.RIGHT_PAREN, $"Expect ')' after {kind} parameters.");

            Consume(TokenType.LEFT_BRACE, $"Expect '{{' before {kind} body.");
            funBodyDepth++;
            Stmt.Block body;
            try
            {
                body = Block();
            }
            finally
            {
                funBodyDepth--;
            }

            return new Stmt.Function(name, parameters, body.statements);
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

        // ifStmt         → "if" "(" expression ")" statement
        //                ( "else" statement )? ;
        private Stmt.If IfStmt()
        {
            Consume(TokenType.LEFT_PAREN, "Expect '(' after 'if'.");
            var condition = Expression();
            Consume(TokenType.RIGHT_PAREN, "Expect ')' after condition.");
            var thenBranch = Statement();

            if (Match(TokenType.ELSE))
            {
                var elseBranch = Statement();
                return new Stmt.If(condition, thenBranch, elseBranch);
            }

            return new Stmt.If(condition, thenBranch, null);
        }

        // whileStmt      → "while" "(" expression ")" statement
        private Stmt.While WhileStmt()
        {
            Consume(TokenType.LEFT_PAREN, "Expect '(' after 'while'.");
            var condition = Expression();
            Consume(TokenType.RIGHT_PAREN, "Expect ')' after condition.");
            loopBodyDepth++;
            Stmt body;
            try
            {
                body = Statement();
            }
            finally
            {
                loopBodyDepth--;
            }

            return new Stmt.While(condition, body);
        }

        // forStmt        → "for" "(" ( varDecl | exprStmt | ";" )
        //                  expression? ";"
        //                  expression? ")" statement ;
        private Stmt ForStmt()
        {
            Consume(TokenType.LEFT_PAREN, "Expect '(' after 'for'.");

            Stmt? initializer = null;
            if (Match(TokenType.VAR))
            {
                initializer = VarDeclaration();
            }
            else if (!Match(TokenType.SEMICOLON))
            {
                initializer = ExprStmt();
            }

            Expr? condition = null;
            if (!Check(TokenType.SEMICOLON))
            {
                condition = Expression();
            }
            Consume(TokenType.SEMICOLON, "Expect ';' after loop condition.");

            Expr? increment = null;
            if (!Check(TokenType.RIGHT_PAREN))
            {
                increment = Expression();
            }
            Consume(TokenType.RIGHT_PAREN, "Expect ')' after for clauses.");

            loopBodyDepth++;
            Stmt body;
            try
            {
                body = Statement();
            }
            finally
            {
                loopBodyDepth--;
            }

            if (increment != null)
            {
                body = new Stmt.Block([
                    body,
                    new Stmt.Expression(increment),
                ]);
            }

            condition ??= new Expr.Literal(true);
            body = new Stmt.While(condition, body);

            if (initializer != null)
            {
                body = new Stmt.Block([
                    initializer,
                    body,
                ]);
            }

            return body;
        }

        // returnStmt     → "return" expression? ";" ;
        private Stmt.Return ReturnStmt()
        {
            Token keyword = Previous();
            Expr? value = null;
            if (!Check(TokenType.SEMICOLON))
            {
                value = Expression();
            }
            Consume(TokenType.SEMICOLON, "Expect ';' after return.");

            return new Stmt.Return(keyword, value);
        }

        // breakStmt      → "break" ";"
        private Stmt.Break BreakStmt()
        {
            Consume(TokenType.SEMICOLON, "Expect ';' after break.");
            return new Stmt.Break();
        }

        // continueStmt   → "continue" ";"
        private Stmt.Continue ContinueStmt()
        {
            Consume(TokenType.SEMICOLON, "Expect ';' after continue.");
            return new Stmt.Continue();
        }

        // expression     → comma ;
        internal Expr Expression()
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

        // ternary        → logic_or ( "?" ternary ":" ternary )? ;
        private Expr Ternary()
        {
            var expr = LogicOr();

            if (Match(TokenType.QUESTION))
            {
                var consequent = Ternary();
                Consume(TokenType.COLON, "Expect ':' after '?'.");
                var alternate = Ternary();
                return new Expr.Ternary(expr, consequent, alternate);
            }

            return expr;
        }

        // logic_or       → logic_and ( "or" logic_and )* ;
        private Expr LogicOr()
        {
            var expr = LogicAnd();

            while (Match(TokenType.OR))
            {
                var @operator = Previous();
                var right = LogicAnd();
                expr = new Expr.Logical(expr, @operator, right);
            }

            return expr;
        }

        // logic_and      → equality ( "and" equality )* ;
        private Expr LogicAnd()
        {
            var expr = Equality();

            while (Match(TokenType.AND))
            {
                var @operator = Previous();
                var right = Equality();
                expr = new Expr.Logical(expr, @operator, right);
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
        //                | call ;
        private Expr Unary()
        {
            if (Match(TokenType.BANG, TokenType.MINUS))
            {
                var @operator = Previous();
                var right = Unary();
                return new Expr.Unary(@operator, right);
            }

            return Call();
        }

        // call           → lambda ( "(" arguments? ")" )* ;
        private Expr Call()
        {
            var expr = Lambda();

            while (true)
            {
                if (Match(TokenType.LEFT_PAREN))
                {
                    expr = FinishCall(expr);
                }
                else
                {
                    break;
                }
            }

            return expr;
        }

        // arguments      → assignment ( "," assignment )* ;
        private Expr.Call FinishCall(Expr callee)
        {
            List<Expr> arguments = [];
            if (!Check(TokenType.RIGHT_PAREN))
            {
                do
                {
                    if (arguments.Count >= 255)
                    {
                        Error(Peek(), "Can't have more than 255 arguments.");
                    }
                    arguments.Add(Assignment());
                }
                while (Match(TokenType.COMMA));
            }

            Token paren = Consume(TokenType.RIGHT_PAREN, "Expect ')' after arguments.");
            return new Expr.Call(callee, paren, arguments);
        }

        // lambda         → primary | ( "fun" "(" parameters? ")" block ) ;
        // parameters     → IDENTIFIER ( "," IDENTIFIER )* ;
        private Expr Lambda()
        {
            if (Match(TokenType.FUN))
            {
                Consume(TokenType.LEFT_PAREN, "Expect '(' after 'fun'.");
                List<Token> parameters = [];
                if (!Check(TokenType.RIGHT_PAREN))
                {
                    do
                    {
                        if (parameters.Count >= 255)
                        {
                            Error(Peek(), "Can't have more than 255 parameters.");
                        }

                        parameters.Add(Consume(TokenType.IDENTIFIER, "Expect parameter name."));
                    } while (Match(TokenType.COMMA));
                }
                Consume(TokenType.RIGHT_PAREN, $"Expect ')' after lambda parameters.");

                Consume(TokenType.LEFT_BRACE, $"Expect '{{' before lambda body.");
                funBodyDepth++;
                Stmt.Block body;
                try
                {
                    body = Block();
                }
                finally
                {
                    funBodyDepth--;
                }

                return new Expr.Lambda(parameters, body.statements);
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

        private bool CheckNext(TokenType type)
        {
            if (current + 1 >= tokens.Count)
            {
                return false;
            }
            return tokens[current + 1].type == type;
        }

        private Token Advance()
        {
            if (!IsAtEnd())
            {
                current++;
            }
            return Previous();
        }

        internal bool IsAtEnd()
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
