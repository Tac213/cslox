namespace cslox
{
    class Interpreter : Expr.IVisitor<object?>, Stmt.IVisitor
    {
        private class BreakLoop : Exception {}
        private class ContinueLoop : Exception {}

        internal Environment globals = new();
        private Environment environment;

        internal Interpreter()
        {
            environment = globals;
            DefineNativeFunctions();
        }

        private void DefineNativeFunctions()
        {
            globals.Define("clock", new NativeFunctions.Clock());
            globals.Define("typeof", new NativeFunctions.TypeOf());
            globals.Define("stringify", new NativeFunctions.Stringify());
        }

        internal void Interpret(List<Stmt> statements)
        {
            try
            {
                foreach (var statement in statements)
                {
                    Execute(statement);
                }
            }
            catch (RuntimeError error)
            {
                Lox.RuntimeError(error);
            }
        }

        public object? VisitBinaryExpr(Expr.Binary expr)
        {
            var left = Evaluate(expr.left);
            var right = Evaluate(expr.right);

            if (expr.@operator.type == TokenType.COMMA)
            {
                return right;
            }

            switch (expr.@operator.type)
            {
                case TokenType.MINUS:
                    if (left is double leftV5 && right is double rightV5)
                    {
                        return leftV5 - rightV5;
                    }
                    throw new RuntimeError(expr.@operator, $"'-' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.SLASH:
                    if (left is double leftV6 && right is double rightV6)
                    {
                        if (rightV6 == 0.0)
                        {
                            throw new RuntimeError(expr.@operator, "Division by zero.");
                        }
                        return leftV6 / rightV6;
                    }
                    throw new RuntimeError(expr.@operator, $"'/' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.STAR:
                    if (left is double leftV7 && right is double rightV7)
                    {
                        return leftV7 * rightV7;
                    }
                    if (left is double leftV8 && double.IsInteger(leftV8) && right is string rightS5)
                    {
                        return string.Concat(Enumerable.Repeat(rightS5, (int)leftV8));
                    }
                    if (left is string leftS5 && right is double rightV8 && double.IsInteger(rightV8))
                    {
                        return string.Concat(Enumerable.Repeat(leftS5, (int)rightV8));
                    }
                    throw new RuntimeError(expr.@operator, $"'*' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.PLUS:
                    if (left is double leftV && right is double rightV)
                    {
                        return leftV + rightV;
                    }
                    if (left is string leftS && right is string rightS)
                    {
                        return leftS + rightS;
                    }
                    if (left is double leftV9 && right is string rightS6)
                    {
                        return leftV9.ToString() + rightS6;
                    }
                    if (left is string leftS6 && right is double rightV9)
                    {
                        return leftS6 + rightV9.ToString();
                    }
                    throw new RuntimeError(expr.@operator, $"'+' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.GREATER:
                    if (left is double leftV1 && right is double rightV1)
                    {
                        return leftV1 > rightV1;
                    }
                    if (left is string leftS1 && right is string rightS1)
                    {
                        return string.CompareOrdinal(leftS1, rightS1) > 0;
                    }
                    throw new RuntimeError(expr.@operator, $"'>' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.GREATER_EQUAL:
                    if (left is double leftV2 && right is double rightV2)
                    {
                        return leftV2 >= rightV2;
                    }
                    if (left is string leftS2 && right is string rightS2)
                    {
                        return string.CompareOrdinal(leftS2, rightS2) >= 0;
                    }
                    throw new RuntimeError(expr.@operator, $"'>=' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.LESS:
                    if (left is double leftV3 && right is double rightV3)
                    {
                        return leftV3 < rightV3;
                    }
                    if (left is string leftS3 && right is string rightS3)
                    {
                        return string.CompareOrdinal(leftS3, rightS3) < 0;
                    }
                    throw new RuntimeError(expr.@operator, $"'<' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.LESS_EQUAL:
                    if (left is double leftV4 && right is double rightV4)
                    {
                        return leftV4 <= rightV4;
                    }
                    if (left is string leftS4 && right is string rightS4)
                    {
                        return string.CompareOrdinal(leftS4, rightS4) <= 0;
                    }
                    throw new RuntimeError(expr.@operator, $"'<=' not supported between '{TypeOf(left)}' and '{TypeOf(right)}'.");
                case TokenType.BANG_EQUAL:
                    return !IsEqual(left, right);
                case TokenType.EQUAL_EQUAL:
                    return IsEqual(left, right);
            }

            return null;
        }

        public object? VisitGroupingExpr(Expr.Grouping expr)
        {
            return Evaluate(expr.expression);
        }

        public object? VisitLiteralExpr(Expr.Literal expr)
        {
            return expr.value;
        }

        public object? VisitLogicalExpr(Expr.Logical expr)
        {
            var left = Evaluate(expr.left);

            if (expr.@operator.type == TokenType.OR)
            {
                if (IsTruthy(left)) return left;
            }
            else
            {
                if (!IsTruthy(left)) return left;
            }
            return Evaluate(expr.right);
        }

        public object? VisitTernaryExpr(Expr.Ternary expr)
        {
            if (IsTruthy(Evaluate(expr.test)))
            {
                return Evaluate(expr.consequent);
            }
            return Evaluate(expr.alternate);
        }

        public object? VisitUnaryExpr(Expr.Unary expr)
        {
            var right = Evaluate(expr.right);

            if (right == null)
            {
                return null;
            }

            switch (expr.@operator.type)
            {
                case TokenType.BANG:
                    return IsTruthy(right);

                case TokenType.MINUS:
                    CheckNumberOperand(expr.@operator, right);
                    return -(double)right;
            }

            return null;
        }

        public object? VisitCallExpr(Expr.Call expr)
        {
            var callee = Evaluate(expr.callee);

            List<object?> arguments = [];
            foreach (var argument in expr.arguments)
            {
                arguments.Add(Evaluate(argument));
            }

            if (callee is ILoxCallable calleeCallable)
            {
                int arity = calleeCallable.Arity();
                if (arguments.Count != arity)
                {
                    string argLexeme = arity <= 1 ? "argument" : "arguments";
                    throw new RuntimeError(expr.paren, $"Expected {arity} {argLexeme} but got {arguments.Count}.");
                }
                return calleeCallable.Call(this, arguments);
            }

            throw new RuntimeError(expr.paren, $"'{TypeOf(callee)}' object is not callable.");
        }

        public object? VisitVariableExpr(Expr.Variable expr)
        {
            return environment.Get(expr.name);
        }

        public object? VisitAssignExpr(Expr.Assign expr)
        {
            var value = Evaluate(expr.value);
            environment.Assign(expr.name, value);
            return value;
        }

        static private void CheckNumberOperand(Token @operator, object? operand)
        {
            if (operand is double) return;
            throw new RuntimeError(@operator, "Operand must be a number.");
        }

        internal void Execute(Stmt stmt)
        {
            stmt.Accept(this);
        }

        internal void ExecuteBlock(List<Stmt> statements, Environment environment)
        {
            var previous = this.environment;
            try
            {
                this.environment = environment;

                foreach (var statement in statements)
                {
                    Execute(statement);
                }
            }
            finally
            {
                this.environment = previous;
            }
        }

        internal object? Evaluate(Expr expr)
        {
            return expr.Accept(this);
        }

        static private bool IsTruthy(object? v)
        {
            if (v == null) return false;
            if (v is bool bV) return bV;
            return true;
        }

        static private bool IsEqual(object? a, object? b)
        {
            if (a == null) return b == null;

            return a.Equals(b);
        }

        static internal string Stringify(object? obj)
        {
            if (obj == null) return "nil";

            var text = obj.ToString();
            if (text == null)
            {
                return "";
            }

            if (obj is double && text.EndsWith(".0"))
            {
                text = text[0..(text.Length -2)];
            }

            return text;
        }

        static internal string TypeOf(object? obj)
        {
            if (obj == null) return "nil";
            if (obj is double) return "number";
            if (obj is string) return "string";
            if (obj is bool) return "bool";
            if (obj is ILoxCallable) return "callable";

            return "object";
        }

        public void VisitBlockStmt(Stmt.Block stmt)
        {
            ExecuteBlock(stmt.statements, new Environment(environment));
        }

        public void VisitExpressionStmt(Stmt.Expression stmt)
        {
            Evaluate(stmt.expression);
        }

        public void VisitPrintStmt(Stmt.Print stmt)
        {
            var value = Evaluate(stmt.expression);
            Console.WriteLine(Stringify(value));
        }

        public void VisitVarStmt(Stmt.Var stmt)
        {
            environment.Declare(stmt.name.lexeme);
            if (stmt.initializer != null)
            {
                var value = Evaluate(stmt.initializer);
                environment.Define(stmt.name.lexeme, value);
            }
        }

        public void VisitIfStmt(Stmt.If stmt)
        {
            if (IsTruthy(Evaluate(stmt.condition)))
            {
                Execute(stmt.thenBranch);
            }
            else if (stmt.elseBranch != null)
            {
                Execute(stmt.elseBranch);
            }
        }

        public void VisitWhileStmt(Stmt.While stmt)
        {
            while (IsTruthy(Evaluate(stmt.condition)))
            {
                try
                {
                    Execute(stmt.body);
                }
                catch (BreakLoop)
                {
                    break;
                }
                catch (ContinueLoop)
                {
                    continue;
                }
            }
        }

        public void VisitBreakStmt(Stmt.Break stmt)
        {
            throw new BreakLoop();
        }

        public void VisitContinueStmt(Stmt.Continue stmt)
        {
            throw new ContinueLoop();
        }

        public void VisitFunctionStmt(Stmt.Function stmt)
        {
            environment.Define(stmt.name.lexeme, new LoxFunction(stmt, environment));
        }

        public void VisitReturnStmt(Stmt.Return stmt)
        {
            object? value = null;
            if (stmt.value != null)
            {
                value = Evaluate(stmt.value);
            }

            throw new Return(value);
        }
    }
}
