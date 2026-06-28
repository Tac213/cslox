namespace cslox
{
    internal interface ILoxCallable
    {
        int Arity();
        object? Call(Interpreter interpreter, List<object?> arguments);
    }
}
