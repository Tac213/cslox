namespace cslox
{
    internal class Lox
    {
        private static readonly Interpreter interpreter = new();
        static bool hadError = false;
        static bool hadRuntimeError = false;
        static bool silentMode = false;

        private abstract record ParseResult;
        private sealed record StatementList(List<Stmt> Statements) : ParseResult;
        private sealed record SingleExpression(Expr Expression) : ParseResult;

        static void Main(string[] args)
        {
            if (args.Length > 1)
            {
                Console.Error.WriteLine("Usage: cslox [script]");
                System.Environment.Exit(64);
            }
            else if (args.Length == 1)
            {
                RunFile(args[0]);
            }
            else
            {
                RunPrompt();
            }
        }

        internal static void Error(int line, string message)
        {
            Report(line, "", message);
            hadError = true;
        }

        internal static void Error(Token token, string message)
        {
            if (token.type == TokenType.EOF)
            {
                Report(token.line, " at end", message);
            }
            else
            {
                Report(token.line, $" at '{token.lexeme}'", message);
            }
            hadError = true;
        }

        internal static void RuntimeError(RuntimeError error)
        {
            Console.Error.WriteLine($"{error.Message}\n[line {error.token.line}]");
            hadRuntimeError = true;
        }

        private static void Report(int line, string where, string message)
        {
            if (silentMode) return;
            Console.Error.WriteLine($"[line {line}] Error{where}: {message}");
        }

        private static void RunFile(string path)
        {
            var bytes = File.ReadAllBytes(path);
            Run(System.Text.Encoding.Default.GetString(bytes));

            // Indicate an error in the exit code.
            if (hadError)
            {
                System.Environment.Exit(65);
            }

            if (hadRuntimeError)
            {
                System.Environment.Exit(70);
            }
        }

        private static void RunPrompt()
        {
            for (; ; )
            {
                Console.Write("> ");
                string? line = Console.ReadLine();
                if (line == null) break;
                Run(line, true);

                // If the user makes a mistake,
                // it shouldn’t kill their entire session.
                hadError = false;
            }
        }

        private static void Run(string source, bool isREPL = false)
        {
            Scanner scanner = new(source);
            var tokens = scanner.ScanTokens();

            Parser parser = new(tokens);
            ParseResult result;
            if (isREPL)
            {
                // Save a copy of tokens in case we need to re-parse as statements.
                Expr? expr = null;
                try
                {
                    silentMode = true;
                    expr = parser.Expression();
                }
                catch (Parser.ParseError)
                {
                }
                finally
                {
                    silentMode = false;
                }
                if (expr != null && parser.IsAtEnd())
                {
                    result = new SingleExpression(expr);
                }
                else
                {
                    // Re-parse from scratch as statements.
                    parser = new(tokens);
                    result = new StatementList(parser.Parse());
                }
            }
            else
            {
                result = new StatementList(parser.Parse());
            }

            // Stop if there was a syntax error.
            if (hadError) return;

            switch (result)
            {
                case StatementList(var statements):
                    interpreter.Interpret(statements);
                    break;
                case SingleExpression(var expr):
                    try
                    {
                        var value = interpreter.Evaluate(expr);
                        Console.WriteLine(Interpreter.Stringify(value));
                    }
                    catch (RuntimeError e)
                    {
                        RuntimeError(e);
                    }
                    break;
            }
        }
    }
}
