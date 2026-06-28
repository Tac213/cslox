namespace cslox
{
    internal class LoxFunction : ILoxCallable
    {
        private readonly Stmt.Function declaration;
        private readonly Environment closure;

        internal LoxFunction(Stmt.Function declaration, Environment closure)
        {
            this.declaration = declaration;
            this.closure = closure;
        }

        public int Arity()
        {
            return declaration.@params.Count;
        }

        public object? Call(Interpreter interpreter, List<object?> arguments)
        {
            Environment environment = new(closure);
            for (int i = 0; i < declaration.@params.Count; i++)
            {
                environment.Define(declaration.@params[i].lexeme, arguments[i]);
            }

            try
            {
                interpreter.ExecuteBlock(declaration.body, environment);
            }
            catch (Return loxReturn)
            {
                return loxReturn.value;
            }
            return null;
        }

        public override string ToString()
        {
            return $"<lox fn {declaration.name.lexeme}>";
        }
    }

    internal class Lambda : ILoxCallable
    {
        private readonly Expr.Lambda declaration;
        private readonly Environment closure;

        internal Lambda(Expr.Lambda declaration, Environment closure)
        {
            this.declaration = declaration;
            this.closure = closure;
        }
        public int Arity()
        {
            return declaration.@params.Count;
        }

        public object? Call(Interpreter interpreter, List<object?> arguments)
        {
            Environment environment = new(closure);
            for (int i = 0; i < declaration.@params.Count; i++)
            {
                environment.Define(declaration.@params[i].lexeme, arguments[i]);
            }

            try
            {
                interpreter.ExecuteBlock(declaration.body, environment);
            }
            catch (Return loxReturn)
            {
                return loxReturn.value;
            }
            return null;
        }

        public override string ToString()
        {
            return "<lambda>";
        }
    }
}
