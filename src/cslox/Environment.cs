using System.Diagnostics;

namespace cslox
{
    class Environment
    {
        private class VarValue
        {
            internal object? value = null;
            internal bool isAssigned = false;
            internal bool isNative = false;
            internal readonly Token name;

            internal VarValue(Token name)
            {
                this.name = name;
            }
        }

        private readonly List<VarValue> values = [];
        internal readonly Environment? enclosing;

        internal Environment()
        {
            enclosing = null;
        }

        internal Environment(Environment? enclosing)
        {
            this.enclosing = enclosing;
        }

        internal object? Get(int index)
        {
            Debug.Assert(index < values.Count, $"Invalid global variable index: {index}, please check resolver.");
            var variable = values[index];
            if (variable.isAssigned) return variable.value;
            throw new RuntimeError(variable.name, $"Accessing a global variable '{variable.name.lexeme}' that has not been initialized or assigned to.");
        }

        internal object? GetAt(int distance, int index)
        {
            var ancestor = Ancestor(distance);
            if (index < ancestor.values.Count)
            {
                var variable = ancestor.values[index];
                if (variable.isAssigned) return variable.value;
                throw new RuntimeError(variable.name, $"Accessing a variable '{variable.name.lexeme}' that has not been initialized or assigned to.");
            }
            throw new RuntimeError(new Token(TokenType.EOF, "", null, 0), "Undefined variable.");
        }

        internal Environment Ancestor(int distance)
        {
            Environment environment = this;
            for (int i = 0; i < distance; i++)
            {
                var enclosing = environment.enclosing;
                Debug.Assert(enclosing != null, $"Resolver bug: distance {distance} exceeds environment depth.");
                environment = enclosing;
            }

            return environment;
        }

        internal void Assign(int index, object? value)
        {
            Debug.Assert(index < values.Count, $"Invalid global variable index: {index}, please check resolver.");
            var variable = values[index];
            variable.value = value;
            variable.isAssigned = true;
        }

        internal void AssignAt(int distance, int index, object? value)
        {
            var ancestor = Ancestor(distance);
            Debug.Assert(index < ancestor.values.Count, $"Invalid local variable index: {index}, please check resolver.");
            var variable = ancestor.values[index];
            variable.value = value;
            variable.isAssigned = true;
        }

        [Conditional("DEBUG")]
        private void VerifyVariableName(string name)
        {
            foreach (var variable in values)
            {
                if (variable.name.lexeme == name)
                {
                    throw new InvalidOperationException($"Variable '{name}' is defined multiple times!");
                }
            }
        }

        // Only for native functions.
        internal void Define(string name, object? value)
        {
            Debug.Assert(value is NativeFunctions.NativeFunction, "This method can only define native functions.");
            VerifyVariableName(name);
            VarValue var = new(new Token(TokenType.IDENTIFIER, name, null, -1))
            {
                value = value,
                isAssigned = true,
                isNative = true,
            };
            values.Add(var);
        }

        internal void Define(Token name, object? value)
        {
            VarValue var = new(name)
            {
                value = value,
                isAssigned = true
            };
            values.Add(var);
        }

        internal void Define(int index, object? value)
        {
            Debug.Assert(index < values.Count, $"Invalid variable index: {index}, please check resolver.");
            var var = values[index];
            var.value = value;
            var.isAssigned = true;
        }

        internal int Declare(Token name)
        {
            int index = values.Count;
            values.Add(new VarValue(name));
            return index;
        }

        internal IEnumerable<string> GetBuiltinFunctionNames()
        {
            Debug.Assert(enclosing == null, "This method can only be called on the global environment.");
            foreach (var variable in values)
            {
                if (variable.isNative)
                {
                    yield return variable.name.lexeme;
                }
            }
        }
    }
}
