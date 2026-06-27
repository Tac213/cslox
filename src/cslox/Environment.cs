namespace cslox
{
    class Environment
    {
        private class VarValue
        {
            internal object? value = null;
            internal bool isAssigned = false;
        }

        private readonly Dictionary<string, VarValue> values = [];
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
            if (values.TryGetValue(name.lexeme, out VarValue? var))
            {
                if (var.isAssigned) return var.value;
                throw new RuntimeError(name, $"Access a variable '{name.lexeme}' that has not been initialized or assigned to.");
            }

            if (enclosing != null) return enclosing.Get(name);

            throw new RuntimeError(name, $"Undefined variable '{name.lexeme}'.");
        }

        internal void Assign(Token name, object? value)
        {
            if (values.TryGetValue(name.lexeme, out VarValue? var))
            {
                var.value = value;
                var.isAssigned = true;
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
            if (values.TryGetValue(name, out VarValue? var))
            {
                var.value = value;
                var.isAssigned = true;
                return;
            }
            var = new()
            {
                value = value,
                isAssigned = true
            };
            values[name] = var;
        }

        internal void Declare(string name)
        {
            if (values.ContainsKey(name))
            {
                return;
            }
            values[name] = new VarValue();
        }

        private bool TryGetVarValue(string name, out VarValue? var)
        {
            if (values.TryGetValue(name, out var))
            {
                return true;
            }
            if (enclosing != null && enclosing.TryGetVarValue(name, out var))
            {
                return true;
            }
            return false;
        }

        internal bool IsDeclared(string name)
        {
            if (values.ContainsKey(name)) return true;
            if (enclosing != null) return enclosing.IsDeclared(name);
            return false;
        }
    }
}
