namespace cslox
{
    class Interpreter : Expr.IVisitor<object?>
    {
        internal void Interpret(Expr expression)
        {
            try
            {
                var value = Evaluate(expression);
                Console.WriteLine(Stringify(value));
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

            if (left == null || right == null)
            {
                return null;
            }

            switch (expr.@operator.type)
            {
                case TokenType.MINUS:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left - (double)right;
                case TokenType.SLASH:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left / (double)right;
                case TokenType.STAR:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left * (double)right;
                case TokenType.PLUS:
                    if (left is double leftV && right is double rightV)
                    {
                        return leftV + rightV;
                    }

                    if (left is string leftS && right is string rightS)
                    {
                        return leftS + rightS;
                    }
                    throw new RuntimeError(expr.@operator, "Operands must be two numbers or two strings.");
                case TokenType.GREATER:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left > (double)right;
                case TokenType.GREATER_EQUAL:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left >= (double)right;
                case TokenType.LESS:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left < (double)right;
                case TokenType.LESS_EQUAL:
                    CheckNumberOperands(expr.@operator, left, right);
                    return (double)left <= (double)right;
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

        static private void CheckNumberOperand(Token @operator, object? operand)
        {
            if (operand is double) return;
            throw new RuntimeError(@operator, "Operand must be a number.");
        }

        static private void CheckNumberOperands(Token @operator, object? left, object? right)
        {
            if (left is double && right is double) return;
            throw new RuntimeError(@operator, "Operands must be numbers.");
        }

        private object? Evaluate(Expr expr)
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

        static private string Stringify(object? obj)
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
    }
}
