using System.Diagnostics;

namespace cslox
{
    class Resolver : Expr.IVisitor<object?>, Stmt.IVisitor
    {
        private enum FunctionType
        {
            NONE,
            FUNCTION,
            INITIALIZER,
            METHOD,
            CLASS_METHOD,
            GETTER,
            SETTER,
            LAMBDA
        }

        private enum ClassType
        {
            NONE,
            CLASS,
            SUPERCLASS
        }

        private class VarState
        {
            internal bool isDefined = false;
            internal bool isUsed = false;
            internal bool isParameter = false;
            internal bool isNative = false;
            internal readonly Token token;
            internal readonly int index = 0;

            internal VarState(Token token, int index)
            {
                this.token = token;
                this.index = index;
            }
        }

        private readonly Interpreter interpreter;
        private bool isREPL = false;
        private readonly Dictionary<string, VarState> globalScope = [];
        private readonly Stack<Dictionary<string, VarState>> scopes = [];
        private FunctionType currentFunction = FunctionType.NONE;
        private ClassType currentClass = ClassType.NONE;
        private bool isInLoop = false;
        private bool isInSwitch = false;
        private readonly List<Token> unusedLocalVariables = [];
        private bool isFirstPass = true;

        internal Resolver(Interpreter interpreter) {
            this.interpreter = interpreter;

            // Initialize native functions in global scope.
            int index = 0;
            foreach (var nativeFnName in interpreter.globals.GetBuiltinFunctionNames())
            {
                globalScope[nativeFnName] = new VarState(new Token(TokenType.IDENTIFIER, nativeFnName, null, -1), index)
                {
                    isDefined = true,
                    isNative = true,
                };
                index++;
            }
        }

        internal void SetIsREPL(bool isREPL)
        {
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

        public void VisitClassStmt(Stmt.Class stmt)
        {
            var enclosingClass = currentClass;
            currentClass = ClassType.CLASS;

            Declare(stmt.name);

            if (stmt.superclass is not null)
            {
                currentClass = ClassType.SUPERCLASS;
                Resolve(stmt.superclass);
            }

            Define(stmt.name);

            if (stmt.superclass is not null)
            {
                BeginScope();
                Token superToken = new(TokenType.SUPER, "super", null, stmt.superclass.name.line);
                Declare(superToken);
                Define(superToken);
                if (scopes.Peek().TryGetValue("super", out var superVar))
                {
                    superVar.isUsed = true;
                }
            }

            BeginScope();
            Token thisToken = new(TokenType.THIS, "this", null, stmt.name.line);
            Declare(thisToken);
            Define(thisToken);
            if (scopes.Peek().TryGetValue("this", out var thisVar))
            {
                thisVar.isUsed = true;
            }

            foreach (var method in stmt.methods)
            {
                var declaration = FunctionType.METHOD;
                if (method.name.lexeme == "init")
                {
                    declaration = FunctionType.INITIALIZER;
                }
                ResolveFunction(method, declaration);
            }

            foreach (var property in stmt.properties)
            {
                if (property.getter is not null)
                {
                    ResolveFunction(property.getter, FunctionType.GETTER);
                }
                if (property.setter is not null)
                {
                    ResolveFunction(property.setter, FunctionType.SETTER);
                }
            }

            EndScope();

            if (stmt.superclass is not null) EndScope();

            currentClass = enclosingClass;

            foreach (var method in stmt.classMethods)
            {
                ResolveFunction(method, FunctionType.CLASS_METHOD);
            }
        }

        public void VisitBreakStmt(Stmt.Break stmt)
        {
            if (!isInLoop && !isInSwitch)
            {
                Lox.Error(stmt.keyword, "Can't break from non-loop body and non-switch body.");
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

        public void VisitSwitchStmt(Stmt.Switch stmt)
        {
            var currentIsInSwitch = isInSwitch;
            Resolve(stmt.value);
            isInSwitch = true;

            for (int i = 0; i < stmt.cases.Count; i++)
            {
                var cases = stmt.cases[i];
                foreach (var caseExpr in cases)
                {
                    Resolve(caseExpr);
                }
                var stmts = stmt.statements[i];
                Resolve(stmts);
            }
            if (stmt.defaultStmts != null)
            {
                foreach(var stmts in stmt.defaultStmts)
                {
                    Resolve(stmts);
                }
            }

            isInSwitch = currentIsInSwitch;
        }

        public object? VisitGetExpr(Expr.Get expr)
        {
            Resolve(expr.@object);
            return null;
        }

        public object? VisitSetExpr(Expr.Set expr)
        {
            Resolve(expr.value);
            Resolve(expr.@object);
            return null;
        }

        public object? VisitSuperExpr(Expr.Super expr)
        {
            if (currentClass == ClassType.NONE)
            {
                Lox.Error(expr.keyword, "Can't use 'super' outside of a class.");
                return null;
            }
            else if (currentClass != ClassType.SUPERCLASS)
            {
                Lox.Error(expr.keyword, "Can't use 'super' in a class with no superclass.");
                return null;
            }

            ResolveLocal(expr, expr.keyword, true);
            return null;
        }

        public object? VisitThisExpr(Expr.This expr)
        {
            if (currentClass == ClassType.NONE)
            {
                Lox.Error(expr.keyword, "Can't use 'this' outside of a class.");
                return null;
            }

            ResolveLocal(expr, expr.keyword, true);
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
            if (currentFunction == FunctionType.INITIALIZER)
            {
                Lox.Error(stmt.keyword, "Can't return a value from an initializer.");
            }
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
            if (isTopLevel) isFirstPass = true;
            foreach (var stmt in statements)
            {
                Resolve(stmt);
            }
            if (isTopLevel)
            {
                isFirstPass = false;
                foreach (var stmt in statements)
                {
                    Resolve(stmt);
                }
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

        private bool ResolveGlobal(Expr expr, Token name, bool isUsing)
        {
            if (globalScope.TryGetValue(name.lexeme, out var state))
            {
                if (isUsing && !state.isDefined)
                {
                    Lox.Error(name, $"Accessing a global variable '{name.lexeme}' that has not been initialized or assigned to.");
                }
                interpreter.ResolveGlobal(expr, state.index);
                if (isUsing) state.isUsed = true;
                return true;
            }
            return false;
        }

        private void ResolveLocal(Expr expr, Token name, bool isUsing)
        {
            for (int i = 0; i < scopes.Count; i++)
            {
                if (scopes.ElementAt(i).TryGetValue(name.lexeme, out var state))
                {
                    if (isFirstPass && isUsing && !state.isDefined)
                    {
                        Lox.Error(name, $"Accessing a local variable '{name.lexeme}' that has not been initialized or assigned to.");
                    }
                    if (isFirstPass) interpreter.ResolveLocal(expr, i, state.index);
                    if (isUsing) state.isUsed = true;
                    return;
                }
            }
            var resolved = ResolveGlobal(expr, name, isUsing);
            if (!resolved && !isFirstPass)
            {
                if (isUsing)
                {
                    Lox.Error(name, $"Accessing undefined variable '{name.lexeme}'.");
                }
                else
                {
                    Lox.Error(name, $"Assigning undefined variable '{name.lexeme}'.");
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
            if (!isFirstPass) return;
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
            if (scopes.Count == 0)
            {
                GlobalDeclare(name);
                return;
            }

            var scope = scopes.Peek();
            if (scope.ContainsKey(name.lexeme) && isFirstPass)
            {
                Lox.Error(name, $"Already a variable named '{name.lexeme}' in this scope.");
                return;
            }

            var index = scope.Count;
            scope[name.lexeme] = new VarState(name, index)
            {
                isParameter = isParameter,
            };
        }

        private void Define(Token name)
        {
            if (scopes.Count == 0)
            {
                GlobalDefine(name);
                return;
            }

            var scope = scopes.Peek();
            if (scope.TryGetValue(name.lexeme, out var state))
            {
                state.isDefined = true;
                return;
            }
            throw new InvalidOperationException("Call 'Declare' first!");
        }

        private void GlobalDeclare(Token name)
        {
            if (!isFirstPass) return;
            if (globalScope.TryGetValue(name.lexeme, out var state))
            {
                if (state.isNative)
                {
                    Lox.Error(name, $"'{name.lexeme}' is a global native function.");
                }
                else
                {
                    Lox.Error(name, $"Already a variable named '{name.lexeme}' globally.");
                }
            }

            var index = globalScope.Count;
            globalScope[name.lexeme] = new VarState(name, index);
        }

        private void GlobalDefine(Token name)
        {
            if (!isFirstPass) return;
            if (globalScope.TryGetValue(name.lexeme, out var state))
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

        public void VisitPropertyStmt(Stmt.Property stmt)
        {
            Debug.Assert(false, "This should never happen.");
        }
    }
}
