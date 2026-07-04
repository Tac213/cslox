namespace cslox
{
    internal class LoxFunction : ILoxCallable
    {
        private readonly Stmt.Function declaration;
        private readonly Environment closure;
        private readonly bool isInitializer;
        private readonly int? thisIndex;

        internal bool IsMethod
        {
            get { return thisIndex != null; }
        }

        internal LoxInstance? BoundInstance
        {
            get
            {
                if (thisIndex is int index)
                {
                    var obj = closure.GetAt(0, index);
                    if (obj is LoxInstance instance) return instance;
                    return null;
                }
                return null;
            }
        }

        internal LoxFunction(Stmt.Function declaration, Environment closure, int? thisIndex = null, bool isInitializer = false)
        {
            this.declaration = declaration;
            this.closure = closure;
            this.thisIndex = thisIndex;
            this.isInitializer = isInitializer;
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
                environment.Define(declaration.@params[i], arguments[i]);
            }

            try
            {
                interpreter.ExecuteBlock(declaration.body, environment);
            }
            catch (Return loxReturn)
            {
                if (isInitializer && thisIndex is int idx)
                {
                    return closure.GetAt(0, idx);
                }
                return loxReturn.value;
            }
            if (isInitializer && thisIndex is int index)
            {
                return closure.GetAt(0, index);
            }
            return null;
        }

        public override string ToString()
        {
            if (BoundInstance is LoxInstance instance)
            {
                return $"<bound method {instance.@class.name}.{declaration.name.lexeme}>";
            }
            return $"<lox fn {declaration.name.lexeme}>";
        }

        internal LoxFunction Bind(LoxInstance instance)
        {
            Environment environment = new(closure);
            Token thisToken = new(TokenType.THIS, "this", null, -1);
            var index  = environment.Declare(thisToken);
            environment.Define(index, instance);
            return new LoxFunction(declaration, environment, index, isInitializer);
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
                environment.Define(declaration.@params[i], arguments[i]);
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
