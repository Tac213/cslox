namespace cslox
{
    internal class Return : Exception
    {
        internal readonly object? value;

        internal Return(object? value) : base()
        {
            this.value = value;
        }
    }
}
