namespace cslox
{
    class RuntimeError : Exception
    {
        internal readonly Token token;

        internal RuntimeError(Token token, string message) : base(message)
        {
            this.token = token;
        }
    }
}
