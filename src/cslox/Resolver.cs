namespace cslox
{
    class Resolver : Expr.IVisitor<object?>, Stmt.IVisitor
    {
        private enum FunctionType
        {
            NONE,
            FUNCTION,
            LAMBDA
        }

        private class LocalVarState
        {
            internal bool isDefined = false;
            internal bool isUsed = false;
            internal bool isParameter = false;
            internal readonly Token token;

            internal LocalVarState(Token token)
            {
                this.token = token;
            }
        }

        private readonly Interpreter interpreter;
        private readonly bool isREPL;
        private readonly Stack<Dictionary<string, LocalVarState>> scopes = [];
        private FunctionType currentFunction = FunctionType.NONE;
        private bool isInLoop = false;
        private readonly List<Token> unusedLocalVariables = [];

        internal Resolver(Interpreter interpreter, bool isREPL) {
            this.interpreter = interpreter;
            this.isREPL = isREPL;
        }

        public object? VisitAssignExpr(Expr.Assign expr)
        {
            Resolve(expr.value);
            ResolveLocal(expr, expr.name, false);
            return null;
        }

        public object? VisitBinaryExpr(Expr.Binary expr)
        {
            Resolve(expr.left);
            Resolve(expr.right);
            return null;
        }

        public void VisitBlockStmt(Stmt.Block stmt)
        {
            BeginScope();
            Resolve(stmt.statements);
            EndScope();
        }

        public void VisitBreakStmt(Stmt.Break stmt)
        {
            if (!isInLoop)
            {
                Lox.Error(stmt.keyword, "Can't break from non-loop body.");
            }
        }

        public object? VisitCallExpr(Expr.Call expr)
        {
            Resolve(expr.callee);

            foreach (var argument in expr.arguments)
            {
                Resolve(argument);
            }

            return null;
        }

        public void VisitContinueStmt(Stmt.Continue stmt)
        {
            if (!isInLoop)
            {
                Lox.Error(stmt.keyword, "Can't continue from non-loop body.");
            }
        }

        public void VisitExpressionStmt(Stmt.Expression stmt)
        {
            Resolve(stmt.expression);
        }

        public void VisitFunctionStmt(Stmt.Function stmt)
        {
            Declare(stmt.name);
            Define(stmt.name);
            ResolveFunction(stmt, FunctionType.FUNCTION);
        }

        public object? VisitGroupingExpr(Expr.Grouping expr)
        {
            Resolve(expr.expression);
            return null;
        }

        public void VisitIfStmt(Stmt.If stmt)
        {
            Resolve(stmt.condition);
            Resolve(stmt.thenBranch);
            if (stmt.elseBranch != null) Resolve(stmt.elseBranch);
        }

        public object? VisitLambdaExpr(Expr.Lambda expr)
        {
            var enclosing = currentFunction;
            currentFunction = FunctionType.LAMBDA;

            BeginScope();
            foreach (var @param in expr.@params)
            {
                Declare(@param, true);
                Define(@param);
            }
            Resolve(expr.body);
            EndScope();

            currentFunction = enclosing;
            return null;
        }

        public object? VisitLiteralExpr(Expr.Literal expr)
        {
            return null;
        }

        public object? VisitLogicalExpr(Expr.Logical expr)
        {
            Resolve(expr.left);
            Resolve(expr.right);
            return null;
        }

        public void VisitPrintStmt(Stmt.Print stmt)
        {
            Resolve(stmt.expression);
        }

        public void VisitReturnStmt(Stmt.Return stmt)
        {
            if (currentFunction == FunctionType.NONE)
            {
                Lox.Error(stmt.keyword, "Can't return from top-level code.");
            }
            if (stmt.value == null) return;
            Resolve(stmt.value);
        }

        public object? VisitTernaryExpr(Expr.Ternary expr)
        {
            Resolve(expr.test);
            Resolve(expr.consequent);
            Resolve(expr.alternate);
            return null;
        }

        public object? VisitUnaryExpr(Expr.Unary expr)
        {
            Resolve(expr.right);
            return null;
        }

        public object? VisitVariableExpr(Expr.Variable expr)
        {
            if (scopes.Count != 0 &&
                scopes.Peek().TryGetValue(expr.name.lexeme, out var state) &&
                !state.isDefined)
            {
                Lox.Error(expr.name, "Can't read local variable in its own initializer.");
            }

            ResolveLocal(expr, expr.name, true);
            return null;
        }

        public void VisitVarStmt(Stmt.Var stmt)
        {
            Declare(stmt.name);
            if (stmt.initializer != null)
            {
                Resolve(stmt.initializer);
            }
            Define(stmt.name);
        }

        public void VisitWhileStmt(Stmt.While stmt)
        {
            Resolve(stmt.condition);
            var currentIsInLoop = isInLoop;
            isInLoop = true;
            Resolve(stmt.body);
            isInLoop = currentIsInLoop;
        }

        internal void Resolve(List<Stmt> statements, bool isTopLevel = false)
        {
            foreach (var stmt in statements)
            {
                Resolve(stmt);
            }

            if (!isREPL && isTopLevel)
            {
                ReportWarning();
            }
        }

        private void Resolve(Stmt stmt)
        {
            stmt.Accept(this);
        }

        internal void Resolve(Expr expr, bool isTopLevel = false)
        {
            expr.Accept(this);

            if (!isREPL && isTopLevel)
            {
                ReportWarning();
            }
        }

        private void ResolveLocal(Expr expr, Token name, bool isUsing)
        {
            for (int i = scopes.Count - 1; i >= 0; i--)
            {
                if (scopes.ElementAt(i).TryGetValue(name.lexeme, out var state))
                {
                    interpreter.Resolve(expr, scopes.Count - 1 - i);
                    if (isUsing) state.isUsed = true;
                    return;
                }
            }
        }

        private void ResolveFunction(Stmt.Function function, FunctionType type)
        {
            var enclosingFunction = currentFunction;
            currentFunction = type;

            BeginScope();
            foreach (var @param in function.@params)
            {
                Declare(@param, true);
                Define(@param);
            }
            Resolve(function.body);
            EndScope();

            currentFunction = enclosingFunction;
        }

        private void BeginScope()
        {
            scopes.Push([]);
        }

        private void EndScope()
        {
            var scope = scopes.Pop();
            foreach (var (_, state) in scope)
            {
                if (!state.isParameter && !state.isUsed)
                {
                    unusedLocalVariables.Add(state.token);
                }
            }
        }

        private void Declare(Token name, bool isParameter = false)
        {
            if (scopes.Count == 0) return;

            var scope = scopes.Peek();
            if (scope.ContainsKey(name.lexeme))
            {
                Lox.Error(name, $"Already a variable named '{name.lexeme}' in this scope.");
            }

            scope[name.lexeme] = new LocalVarState(name)
            {
                isParameter = isParameter,
            };
        }

        private void Define(Token name)
        {
            if (scopes.Count == 0) return;

            var scope = scopes.Peek();
            if (scope.TryGetValue(name.lexeme, out var state))
            {
                state.isDefined = true;
                return;
            }
            throw new InvalidOperationException("Call 'Declare' first!");
        }

        private void ReportWarning()
        {
            // Unused local variables.
            foreach (var token in unusedLocalVariables)
            {
                Lox.Warning(token, $"Unused local variable: {token.lexeme}");
            }
            unusedLocalVariables.Clear();
        }
    }
}
