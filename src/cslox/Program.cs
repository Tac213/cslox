using System;
using System.IO;

namespace cslox
{
    internal class Lox
    {
        static bool hadError = false;

        static void Main(string[] args)
        {
            if (args.Length > 1)
            {
                Console.Error.WriteLine("Usage: cslox [script]");
                Environment.Exit(64);
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

        private static void Report(int line, string where, string message)
        {
            Console.Error.WriteLine($"[line {line}] Error{where}: {message}");
        }

        private static void RunFile(string path)
        {
            var bytes = File.ReadAllBytes(path);
            Run(System.Text.Encoding.Default.GetString(bytes));

            // Indicate an error in the exit code.
            if (hadError)
            {
                Environment.Exit(65);
            }
        }

        private static void RunPrompt()
        {
            for (; ; )
            {
                Console.Write("> ");
                string? line = Console.ReadLine();
                if (line == null) break;
                Run(line);

                // If the user makes a mistake,
                // it shouldn’t kill their entire session.
                hadError = false;
            }
        }

        private static void Run(string path)
        {
            Scanner scanner = new(path);
            var tokens = scanner.ScanTokens();
            foreach (var token in tokens)
            {
                Console.WriteLine(token);
            }
        }
    }
}
