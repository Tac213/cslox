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

        public string VisitUnaryExpr(Expr.Unary expr)
        {
            return Parenthesize(expr.@operator.lexeme, expr.right);
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

            return builder.ToString();
        }
    }
}
