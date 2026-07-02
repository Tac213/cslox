namespace cslox
{
    namespace NativeFunctions
    {
        internal abstract class NativeFunction : ILoxCallable
        {
            public abstract string Name();
            public abstract int Arity();
            public abstract object? Call(Interpreter interpreter, List<object?> arguments);

            public override string ToString()
            {
                return $"<native fn {Name()}>";
            }
        }

        internal class Clock : NativeFunction
        {
            public override int Arity()
            {
                return 0;
            }

            public override object? Call(Interpreter interpreter, List<object?> arguments)
            {
                return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() / 1000.0;
            }

            public override string Name()
            {
                return "clock";
            }
        }

        internal class TypeOf : NativeFunction
        {
            public override int Arity()
            {
                return 1;
            }

            public override object? Call(Interpreter interpreter, List<object?> arguments)
            {
                return Interpreter.TypeOf(arguments[0]);
            }

            public override string Name()
            {
                return "typeof";
            }
        }

        internal class Stringify : NativeFunction
        {
            public override int Arity()
            {
                return 1;
            }

            public override object? Call(Interpreter interpreter, List<object?> arguments)
            {
                return Interpreter.Stringify(arguments[0]);
            }

            public override string Name()
            {
                return "stringify";
            }
        }
    }
}
