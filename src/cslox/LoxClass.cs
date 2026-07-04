using System.Diagnostics.CodeAnalysis;

namespace cslox
{
    internal class LoxClass : LoxInstance, ILoxCallable
    {
        internal string name;
        private readonly Dictionary<string, LoxFunction> methods;

        internal LoxClass(
            string name,
            Dictionary<string, LoxFunction> methods,
            Dictionary<string, LoxFunction> class_methods,
            LoxClass? @class) : base(@class)
        {
            this.name = name;
            this.methods = methods;

            foreach (var (method_name, method) in class_methods)
            {
                method.BindClass(this);
                Set(method_name, method);
            }
        }

        public int Arity()
        {
            if (FindMethod("init", out var initializer))
            {
                return initializer.Arity();
            }
            return 0;
        }

        public object? Call(Interpreter interpreter, List<object?> arguments)
        {
            LoxInstance instance = new(this);
            if (FindMethod("init", out var initializer))
            {
                initializer.Bind(instance).Call(interpreter, arguments);
            }
            return instance;
        }

        public override string ToString()
        {
            return $"<lox class {name}>";
        }

        internal bool FindMethod(string name, [MaybeNullWhen(false)] out LoxFunction method)
        {
            if (methods.TryGetValue(name, out var function))
            {
                method = function;
                return true;
            }
            method = null;
            return false;
        }
    }
}
