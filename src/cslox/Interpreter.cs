using System.Diagnostics;

namespace cslox
{
    class Interpreter : Expr.IVisitor<object?>, Stmt.IVisitor
    {
        private class BreakLoop : Exception {}
        private class ContinueLoop : Exception {}

        private class VariableState
        {
            internal int scopeDistance = 0;
            internal int index = 0;
        }

        // base metaclass of all user-defined class.
        internal static LoxClass type = new("type", null, [], [], [], null);

        internal Environment globals = new();
        private Environment environment;
        private readonly Dictionary<Expr, VariableState> locals = [];
        private readonly Dictionary<Expr, VariableState> globalStates = [];

        internal Interpreter()
        {
            environment = globals;
            DefineNativeFunctions();
        }

        private void DefineNativeFunctions()
        {
            var clock = new NativeFunctions.Clock();
            globals.Define(clock.Name(), clock);
            var @typeof = new NativeFunctions.TypeOf();
            globals.Define(@typeof.Name(), @typeof);
            var stringify = new NativeFunctions.Stringify();
            globals.Define(stringify.Name(), stringify);
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

            switch (expr.@operator.type)
            {
                case TokenType.BANG:
                    return !IsTruthy(right);

                case TokenType.MINUS:
                    CheckNumberOperand(expr.@operator, right);
                    if (right == null)
                    {
                        return null;
                    }
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

        public object? VisitGetExpr(Expr.Get expr)
        {
            var obj = Evaluate(expr.@object);
            if (obj is LoxInstance instance)
            {
                return instance.Get(expr.name);
            }

            throw new RuntimeError(expr.name, "Only instances have properties.");
        }

        public object? VisitSetExpr(Expr.Set expr)
        {
            var obj = Evaluate(expr.@object);
            if (obj is LoxInstance instance)
            {
                var value = Evaluate(expr.value);
                instance.Set(expr.name, value);
                return value;
            }

            throw new RuntimeError(expr.name, "Only instances have fields.");
        }

        public object? VisitSuperExpr(Expr.Super expr)
        {
            if (locals.TryGetValue(expr, out var variableState))
            {
                var value = environment.GetAt(variableState.scopeDistance, variableState.index);
                var valueThis = environment.GetAt(variableState.scopeDistance - 1, 0);
                if (value is LoxClass superclass && valueThis is LoxInstance instance)
                {
                    if (superclass.FindMethod(expr.method.lexeme, out var method))
                    {
                        return method.Bind(instance);
                    }
                    else
                    {
                        throw new RuntimeError(expr.method, $"{instance} has no attribute '{expr.method.lexeme}'.");
                    }
                }
                throw new InvalidOperationException("Accessing invalid superclass, please check resolver.");
            }
            Debug.Assert(variableState is not null);
            throw new InvalidOperationException("Accessing invalid superclass, please check resolver.");
        }

        public object? VisitThisExpr(Expr.This expr)
        {
            return LookUpVariable(expr.keyword, expr);
        }

        public object? VisitLambdaExpr(Expr.Lambda expr)
        {
            return new Lambda(expr, environment);
        }

        public object? VisitVariableExpr(Expr.Variable expr)
        {
            return LookUpVariable(expr.name, expr);
        }

        public object? VisitAssignExpr(Expr.Assign expr)
        {
            var value = Evaluate(expr.value);

            if (locals.TryGetValue(expr, out var variableState))
            {
                environment.AssignAt(variableState.scopeDistance, variableState.index, value);
            }
            else if (globalStates.TryGetValue(expr, out variableState))
            {
                globals.Assign(variableState.index, value);
            }
            Debug.Assert(variableState != null);
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
            if (obj is NativeFunctions.NativeFunction) return "native function";
            if (obj is LoxClass) return "class";
            if (obj is LoxInstance loxObj && loxObj.@class is not null) return loxObj.@class.name;
            if (obj is LoxFunction fun)
            {
                if (fun.IsClassMethod)
                {
                    return "class method";
                }
                else if (fun.IsMethod)
                {
                    return "method";
                }
                return "function";
            }

            return "object";
        }

        internal void ResolveGlobal(Expr expr, int index)
        {
            if (globalStates.TryGetValue(expr, out var variableState))
            {
                variableState.index = index;
                return;
            }
            globalStates[expr] = new VariableState()
            {
                index = index,
            };
        }

        internal void ResolveLocal(Expr expr, int depth, int index)
        {
            if (locals.TryGetValue(expr, out var variableState))
            {
                variableState.scopeDistance = depth;
                variableState.index = index;
                return;
            }
            locals[expr] = new VariableState()
            {
                scopeDistance = depth,
                index = index,
            };
        }

        private object? LookUpVariable(Token name, Expr expr)
        {
            if (locals.TryGetValue(expr, out var variableState))
            {
                return environment.GetAt(variableState.scopeDistance, variableState.index);
            }
            if (globalStates.TryGetValue(expr, out variableState))
            {
                return globals.Get(variableState.index);
            }
            Debug.Assert(variableState != null);
            throw new InvalidOperationException($"Accessing invalid variable '{name.lexeme}', please check resolver.");
        }

        public void VisitBlockStmt(Stmt.Block stmt)
        {
            ExecuteBlock(stmt.statements, new Environment(environment));
        }

        public void VisitClassStmt(Stmt.Class stmt)
        {
            LoxClass? superclass = null;
            if (stmt.superclass is not null)
            {
                var evaluatedSuperclass= Evaluate(stmt.superclass);
                if (evaluatedSuperclass is LoxClass @base)
                {
                    superclass = @base;
                }
                else
                {
                    throw new RuntimeError(stmt.superclass.name, "Superclass must be a class.");
                }
            }

            var index = environment.Declare(stmt.name);

            if (superclass is not null)
            {
                environment = new Environment(environment);
                Token superToken = new(TokenType.SUPER, "super", null, stmt.name.line);
                var superIndex = environment.Declare(superToken);
                environment.Define(superIndex, superclass);
            }

            Dictionary<string, LoxFunction> methods = [];
            Dictionary<string, LoxFunction> classMethods = [];
            Dictionary<string, LoxProperty> properties = [];
            foreach (var method in stmt.methods)
            {
                LoxFunction function = new(method, environment, null, method.name.lexeme.Equals("init"));
                methods[method.name.lexeme] = function;
            }

            foreach (var method in stmt.classMethods)
            {
                LoxFunction function = new(method, environment, null, false);
                classMethods[method.name.lexeme] = function;
            }

            foreach (var property in stmt.properties)
            {
                LoxFunction? getter = null;
                if (property.getter is not null)
                {
                    getter = new(property.getter, environment, null, false);
                }

                LoxFunction? setter = null;
                if (property.setter is not null)
                {
                    setter = new(property.setter, environment, null, false);
                }

                properties[property.name.lexeme] = new(property.name, getter, setter);
            }

            if (superclass is not null)
            {
                environment = environment.Ancestor(1);
            }

            environment.Define(
                index,
                new LoxClass(
                    stmt.name.lexeme,
                    superclass,
                    methods,
                    classMethods,
                    properties,
                    type
            ));
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
            var index = environment.Declare(stmt.name);
            if (stmt.initializer != null)
            {
                var value = Evaluate(stmt.initializer);
                environment.Define(index, value);
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
            environment.Define(stmt.name, new LoxFunction(stmt, environment));
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

        public void VisitPropertyStmt(Stmt.Property stmt)
        {
            Debug.Assert(false, "This should never happen.");
        }
    }
}
