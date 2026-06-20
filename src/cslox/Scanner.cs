namespace cslox
{
    class Scanner
    {
        private readonly string source;

        public Scanner(string source)
        {
            this.source = source;
        }

        public List<Token> ScanTokens()
        {
            List<Token> tokens = [];
            return tokens;
        }
    }
}
