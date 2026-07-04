namespace cslox
{
    internal class LoxProperty
    {
        internal readonly Token name;
        internal readonly LoxFunction? getter;
        internal readonly LoxFunction? setter;

        internal LoxProperty(Token name, LoxFunction? getter, LoxFunction? setter)
        {
            this.name = name;
            this.getter = getter;
            this.setter = setter;
        }

        internal object? Get(LoxInstance instance, Token name)
        {
            if (getter is not null)
            {
                return getter.Bind(instance).Call(Lox.interpreter, []);
            }
            throw new RuntimeError(name, $"Property '{this.name.lexeme}' has not getter.");
        }

        internal void Set(LoxInstance instance, Token name, object? value)
        {
            if (setter is not null)
            {
                setter.Bind(instance).Call(Lox.interpreter, [value]);
                return;
            }
            throw new RuntimeError(name, $"Property '{this.name.lexeme}' has not setter.");
        }
    }
}
