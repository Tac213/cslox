namespace cslox
{
    internal class LoxInstance
    {
        internal LoxClass @class;

        private readonly Dictionary<string, object?> fields = [];

        internal LoxInstance(LoxClass @class)
        {
            this.@class = @class;
        }

        public override string ToString()
        {
            return $"<{@class.name} instance>";
        }

        internal object? Get(Token name)
        {
            if (fields.TryGetValue(name.lexeme, out var field))
            {
                return field;
            }
            if (@class.FindMethod(name.lexeme, out var method))
            {
                return method.Bind(this);
            }

            throw new RuntimeError(name, $"{@class.name} instance has no attribute '{name.lexeme}'.");
        }

        internal void Set(Token name, object? value)
        {
            fields[name.lexeme] = value;
        }
    }
}
