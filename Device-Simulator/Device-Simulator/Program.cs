using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Serilog;
using Microsoft.Extensions.DependencyInjection;
using System.Reflection;
using Device_Simulator.Services;
using System.Runtime.Intrinsics.Arm;
using Device_Simulator.Models;

namespace Device_Simulator
{
    internal class Program
    {
        private static readonly CancellationTokenSource _cancellationTokenSource = new CancellationTokenSource();
        static void Main(string[] args)
        {
            Console.CancelKeyPress += (sender, e) =>
            {
                // We'll stop the process manually by using the CancellationToken
                e.Cancel = true;

                // Change the state of the CancellationToken to "Canceled"
                // - Set the IsCancellationRequested property to true
                // - Call the registered callbacks
                _cancellationTokenSource.Cancel();
            };

            ConfigurationBuilder builder = new ConfigurationBuilder();
            BuildConfig(builder);

            IConfiguration config = builder.Build();

            Log.Logger = new LoggerConfiguration()
                .ReadFrom.Configuration(config)
                .CreateLogger();

            Log.Logger.Information("Application Starting");

            List<MqttClientSettings>? mqttClients = config.GetSection("MqttClients").Get<List<MqttClientSettings>>();
            if (mqttClients == null)
                throw new ArgumentException("Missing MqttClients key in appsettings.json!");

            SemaphoreSlim _concurrentProcesses = new SemaphoreSlim(config.GetValue<int>("ConcurrentProcesses", 1)); ;

            List<MqttService> mqttServices = new List<MqttService>();

            foreach (var mqttClient in mqttClients)
            {
                MqttService mqttService = new MqttService(_cancellationTokenSource, config, _concurrentProcesses, mqttClient);
                mqttServices.Add(mqttService);
                mqttService.Run();
            }

            // Keep the applikation running until cancelled.
            while (!_cancellationTokenSource.Token.IsCancellationRequested) { };
        }

        static void BuildConfig(IConfigurationBuilder builder)
        {
            builder.SetBasePath(Directory.GetCurrentDirectory())
                .AddJsonFile("appsettings.json", optional: false, reloadOnChange: true)
                .AddJsonFile("MqttClients.json", optional: false, reloadOnChange: true)
                .AddJsonFile($"appsettings.{Environment.GetEnvironmentVariable("ASPNETCORE_ENVIRONMENT") ?? "Production"}.json", optional: true)
                .AddUserSecrets(Assembly.GetExecutingAssembly(), true)
                .AddEnvironmentVariables();
        }
    }
}
