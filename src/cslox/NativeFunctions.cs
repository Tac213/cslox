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

        internal class StringStartsWith : NativeFunction
        {
            public override int Arity()
            {
                return 2;
            }

            public override object? Call(Interpreter interpreter, List<object?> arguments)
            {
                var strObj = arguments[0];
                var prefixObj = arguments[1];
                if (strObj is string str)
                {
                    if (prefixObj is string prefix)
                    {
                        return str.StartsWith(prefix);
                    }
                    else
                    {
                        throw new RuntimeError(
                            new Token(TokenType.IDENTIFIER, Name(), null, -1),
                            $"Argument 2 has incorrect type, expected 'string', got '{Interpreter.TypeOf(prefixObj)}'."
                        );
                    }
                }
                else
                {
                    throw new RuntimeError(
                        new Token(TokenType.IDENTIFIER, Name(), null, -1),
                        $"Argument 1 has incorrect type, expected 'string', got '{Interpreter.TypeOf(strObj)}'."
                    );
                }
            }

            public override string Name()
            {
                return "startswith";
            }
        }

        internal class StringEndsWith : NativeFunction
        {
            public override int Arity()
            {
                return 2;
            }

            public override object? Call(Interpreter interpreter, List<object?> arguments)
            {
                var strObj = arguments[0];
                var prefixObj = arguments[1];
                if (strObj is string str)
                {
                    if (prefixObj is string prefix)
                    {
                        return str.EndsWith(prefix);
                    }
                    else
                    {
                        throw new RuntimeError(
                            new Token(TokenType.IDENTIFIER, Name(), null, -1),
                            $"Argument 2 has incorrect type, expected 'string', got '{Interpreter.TypeOf(prefixObj)}'."
                        );
                    }
                }
                else
                {
                    throw new RuntimeError(
                        new Token(TokenType.IDENTIFIER, Name(), null, -1),
                        $"Argument 1 has incorrect type, expected 'string', got '{Interpreter.TypeOf(strObj)}'."
                    );
                }
            }

            public override string Name()
            {
                return "endswith";
            }
        }
    }
}
