using System.Text;

namespace cslox
{
    class AstPrinter : Expr.IVisitor<string>
    {
        internal string Print(Expr expr)
        {
            return expr.Accept(this);
        }

        public string VisitBinaryExpr(Expr.Binary expr)
        {
            return Parenthesize(expr.@operator.lexeme, expr.left, expr.right);
        }

        public string VisitTernaryExpr(Expr.Ternary expr)
        {
            StringBuilder builder = new();

            builder.Append('(');
            builder.Append(expr.test.Accept(this));
            builder.Append(" ? ");
            builder.Append(expr.consequent.Accept(this));
            builder.Append(" : ");
            builder.Append(expr.alternate.Accept(this));
            builder.Append(')');

            return builder.ToString();
        }

        public string VisitGroupingExpr(Expr.Grouping expr)
        {
            return Parenthesize("group", expr.expression);
        }

        public string VisitLiteralExpr(Expr.Literal expr)
        {
            var value = expr.value;
            if (value == null)
            {
                return "nil";
            }
            return value.ToString() ?? "nil";
        }

        public string VisitLogicalExpr(Expr.Logical expr)
        {
            return Parenthesize(expr.@operator.lexeme, expr.left, expr.right);
        }

        public string VisitUnaryExpr(Expr.Unary expr)
        {
            return Parenthesize(expr.@operator.lexeme, expr.right);
        }

        public string VisitVariableExpr(Expr.Variable expr)
        {
            return $"(var {expr.name})";
        }

        public string VisitAssignExpr(Expr.Assign expr)
        {
            return $"({expr.name} = {expr.value})";
        }

        public string VisitCallExpr(Expr.Call expr)
        {
            StringBuilder builder = new();

            builder.Append("(call ").Append(expr.callee.Accept(this));

            builder.Append('(');
            var argCount = expr.arguments.Count;
            for (int i = 0; i < argCount; i++)
            {
                var arg = expr.arguments[i];
                builder.Append(arg.Accept(this));
                if (i < argCount - 1)
                {
                    builder.Append(", ");
                }
            }
            builder.Append("))");
            return builder.ToString();
        }

        private string Parenthesize(string name, params Expr[] exprs)
        {
            StringBuilder builder = new();

            builder.Append('(').Append(name);
            foreach (var expr in exprs)
            {
                builder.Append(' ');
                builder.Append(expr.Accept(this));
            }
            builder.Append(')');

            return builder.ToString();
        }

        public string VisitLambdaExpr(Expr.Lambda expr)
        {
            return "(lambda)";
        }
    }
}
