using System.Text;

namespace cslox
{
    class Environment
    {
        private readonly Dictionary<string, object?> values = [];
        internal readonly Environment? enclosing;

        internal Environment()
        {
            enclosing = null;
        }

        internal Environment(Environment? enclosing)
        {
            this.enclosing = enclosing;
        }

        internal object? Get(Token name)
        {
            if (values.TryGetValue(name.lexeme, out object? value))
            {
                return value;
            }

            if (enclosing != null) return enclosing.Get(name);

            throw new RuntimeError(name, $"Undefined variable '{name.lexeme}'.");
        }

        internal void Assign(Token name, object? value)
        {
            if (values.ContainsKey(name.lexeme))
            {
                values[name.lexeme] = value;
                return;
            }

            if (enclosing != null)
            {
                enclosing.Assign(name, value);
                return;
            }

            throw new RuntimeError(name, $"Undefined variable '{name.lexeme}'.");
        }

        internal void Define(string name, object? value)
        {
            values[name] = value;
        }
    }
}
