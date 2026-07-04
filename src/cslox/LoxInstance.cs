namespace cslox
{
    internal class LoxInstance
    {
        internal LoxClass? @class;

        protected readonly Dictionary<string, object?> fields = [];

        internal LoxInstance(LoxClass? @class)
        {
            this.@class = @class;
        }

        public override string ToString()
        {
            if (@class is not null) return $"<{@class.name} instance>";
            return "<native class type>";
        }

        internal object? Get(Token name)
        {
            if (fields.TryGetValue(name.lexeme, out var field))
            {
                return field;
            }
            if (@class is not null)
            {
                if (@class.FindProperty(name.lexeme, out var property))
                {
                    return property.Get(this, name);
                }
                if (@class.FindMethod(name.lexeme, out var method))
                {
                    return method.Bind(this);
                }
            }

            if (@class is not null) throw new RuntimeError(name, $"{@class.name} instance has no attribute '{name.lexeme}'.");
            throw new RuntimeError(name, $"native class 'type' has no attribute '{name.lexeme}'.");
        }

        internal void Set(Token name, object? value)
        {
            if (@class is not null && @class.FindProperty(name.lexeme, out var property))
            {
                property.Set(this, name, value);
                return;
            }
            fields[name.lexeme] = value;
        }

        protected void Set(string name, object? value)
        {
            fields[name] = value;
        }
    }
}
