namespace cslox
{
    namespace NativeFunctions
    {
        internal class Clock : ILoxCallable
        {
            public int Arity()
            {
                return 0;
            }

            public object? Call(Interpreter interpreter, List<object?> arguments)
            {
                return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() / 1000.0;
            }

            public override string ToString()
            {
                return "<native fn clock>";
            }
        }

        internal class TypeOf : ILoxCallable
        {
            public int Arity()
            {
                return 1;
            }

            public object? Call(Interpreter interpreter, List<object?> arguments)
            {
                return Interpreter.TypeOf(arguments[0]);
            }

            public override string ToString()
            {
                return "<native fn typeof>";
            }
        }

        internal class Stringify : ILoxCallable
        {
            public int Arity()
            {
                return 1;
            }

            public object? Call(Interpreter interpreter, List<object?> arguments)
            {
                return Interpreter.Stringify(arguments[0]);
            }

            public override string ToString()
            {
                return "<native fn stringify>";
            }
        }
    }
}
