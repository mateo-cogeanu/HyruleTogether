using BOTW.DedicatedServer;
using BOTW.Logging;

DedicatedServer? dedicatedServer = null;
string? pidFile = Environment.GetEnvironmentVariable("HYRULE_SERVER_PID_FILE");
try
{
	if (!string.IsNullOrWhiteSpace(pidFile))
		File.WriteAllText(pidFile, Environment.ProcessId.ToString());
    Console.WriteLine("***************************************************************");
    Console.WriteLine("*                                                             *");
    Console.WriteLine("*              Hyrule Together Dedicated Server               *");
    Console.WriteLine("*                                                             *");
    Console.WriteLine("***************************************************************\n");

    Console.Write("VERSION: ");
#if (DEBUG)
    Console.WriteLine("DEV");
#else
    Console.WriteLine("2.0.1");
#endif

    Console.WriteLine();

    string? configPath = null;
    bool nonInteractive = false;
    for (int index = 0; index < args.Length; index++)
    {
        if (args[index] == "--config" && index + 1 < args.Length)
            configPath = Path.GetFullPath(args[++index]);
        else if (args[index] == "--non-interactive")
            nonInteractive = true;
        else if (args[index] is "--help" or "-h")
        {
            Console.WriteLine("Usage: MBL.DedicatedServer [--config PATH] [--non-interactive]");
            return;
        }
    }

    dedicatedServer = new DedicatedServer(configPath);
    Logger.Start(Logger.LogLevelEnum.DEBUG, Logger.LogLevelEnum.WARNING);

    dedicatedServer.CopyAppdataFiles();

    dedicatedServer.setupCommands();

    if (!dedicatedServer.setup(nonInteractive))
        Environment.ExitCode = 1;
    else
    {
        Console.CancelKeyPress += (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            dedicatedServer.Shutdown();
        };

        string? input;
        while ((input = Logger.LogInput("")) != null)
            dedicatedServer.process_commands(input);
    }
}
catch (Exception e)
{
    Logger.LogCritical(e.ToString());
    Environment.ExitCode = 1;
}
finally
{
    dedicatedServer?.Shutdown();
	if (!string.IsNullOrWhiteSpace(pidFile))
	{
		try { File.Delete(pidFile); } catch { }
	}
}
