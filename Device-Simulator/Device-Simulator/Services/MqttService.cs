using Microsoft.Extensions.Configuration;
using MQTTnet.Client;
using MQTTnet;
using System.Text;
using Device_Simulator.Models;
using Newtonsoft.Json;

namespace Device_Simulator.Services
{
    public class MqttService
    {
        internal Serilog.ILogger _logger => Serilog.Log.ForContext<MqttService>();
        internal readonly IConfiguration _config;
        internal readonly CancellationToken _cancellationToken;

        // Application settings
        internal readonly string _clientId;
        internal readonly string _clientType = "Sensor";
        internal readonly SemaphoreSlim _concurrentProcesses;

        // MQTT
        internal readonly MqttFactory _mqttFactory;
        internal readonly IMqttClient _mqttClient;
        internal readonly MqttClientSettings _mqttClientSettings;
        // Topics
        internal readonly string _mqttTopicPing;
        internal readonly string _mqttTopicBeacon = "device/inbound/beacon";
        internal readonly string _mqttTopicSettings;
        internal readonly string _mqttTopicChangestate;
        internal readonly string _mqttTelemetry = "telemetry";

        public MqttService(CancellationTokenSource cts, IConfiguration config, SemaphoreSlim concurrentProcesses, MqttClientSettings mqttClientSettings)
        {
            _cancellationToken = cts.Token;
            _config = config;

            // Application settings
            _clientId = _config.GetValue<string>("ClientId")!;
            _clientType = _config.GetValue<string>("ClientType")!;
            _concurrentProcesses = concurrentProcesses;

            // MQTT
            _mqttFactory = new MqttFactory();
            _mqttClient = _mqttFactory.CreateMqttClient();
            _mqttClientSettings = mqttClientSettings;

            _mqttTopicPing = $"device/outbound/ping";
            _mqttTopicSettings = $"device/outbound/{_clientId}/settings";
            _mqttTopicChangestate = $"device/outbound/{_clientId}/changestate";
        }

        public void Run()
        {

            _ = Task.Run(() => StartWorker());
            _ = Task.Run(() => SimulateDevice());
        }

        internal async Task StartWorker()
        {
            try
            {
                _mqttClient.ApplicationMessageReceivedAsync += async ea =>
                {
                    // Wait for an available process, before creating a new Task.
                    await _concurrentProcesses.WaitAsync(_cancellationToken).ConfigureAwait(false);
                    try
                    {
                        _ = Task.Run(async () => await ProcessApplicationMessageReceivedAsync(ea), _cancellationToken);
                    }
                    finally
                    {
                        _concurrentProcesses.Release();
                    }
                };

                await HandleMqttConnection();

                _logger.Information("Cancellation requested. Exiting...");
            }
            catch (Exception ex)
            {
                _logger.Error(ex, "Connection failed.");
            }
        }

        private async Task ProcessApplicationMessageReceivedAsync(MqttApplicationMessageReceivedEventArgs ea)
        {
            string? payload = Encoding.UTF8.GetString(ea.ApplicationMessage.PayloadSegment);
            _logger.Debug($"Received message \"{payload}\", topic \"{ea.ApplicationMessage.Topic}\", ResponseTopic: \"{ea.ApplicationMessage.ResponseTopic}\"");

            await OnTopic(ea, payload);
        }

        private async Task HandleMqttConnection()
        {
            MqttClientOptions mqttClientOptions = GetMqttClientOptionsBuilder().Build();
            List<MqttClientSubscribeOptions> subscribeOptions = GetSubScriptionOptions();

            // Handle reconnection logic and cancellation token properly
            while (!_cancellationToken.IsCancellationRequested)
            {
                try
                {
                    // Periodically check if the connection is alive, otherwise reconnect
                    if (!await _mqttClient.TryPingAsync())
                    {
                        _logger.Information("Attempting to connect to MQTT Broker...");
                        await _mqttClient.ConnectAsync(mqttClientOptions, _cancellationToken);

                        foreach (var subscribeOption in subscribeOptions)
                        {
                            await _mqttClient.SubscribeAsync(subscribeOption, _cancellationToken);
                            _logger.Information($"MQTT client subscribed to topic: {subscribeOption}.");
                        }

                        // Method to override.
                        await OnConnect();
                    }
                }
                catch (Exception ex)
                {
                    _logger.Error(ex, "An error occurred during MQTT operation.");
                }

                // Check the connection status every 5 seconds
                await Task.Delay(TimeSpan.FromSeconds(5), _cancellationToken);
            }
        }

        internal async Task OnConnect()
        {
            await PublishBeacon();
        }

        public async Task PublishMessage(MqttApplicationMessage? msg)
        {
            if (await _mqttClient.TryPingAsync())
            {
                try
                {
                    await _mqttClient.PublishAsync(msg, _cancellationToken);
                }
                catch (Exception ex)
                {
                    _logger.Error(ex, "PulbishMessage: Connection failed.");
                }
            }
        }

        private MqttClientOptionsBuilder GetMqttClientOptionsBuilder()
        {
            MqttClientOptionsBuilder mqttClientOptionsBuilder = new MqttClientOptionsBuilder()
                    .WithTcpServer(_mqttClientSettings.Host, _mqttClientSettings.Port) // MQTT broker address and port
                    .WithCredentials(_mqttClientSettings.Username, _mqttClientSettings.Password) // Set username and password
                    .WithClientId(_mqttClientSettings.ClientId)
                    .WithProtocolVersion(MQTTnet.Formatter.MqttProtocolVersion.V500);

            if (_mqttClientSettings.UseTLS)
            {
                mqttClientOptionsBuilder.WithTlsOptions(
                o => o.WithCertificateValidationHandler(
                    // The used public broker sometimes has invalid certificates. This sample accepts all
                    // certificates. This should not be used in live environments.
                    _ => true));
            }

            return mqttClientOptionsBuilder;
        }

        internal async Task OnTopic(MqttApplicationMessageReceivedEventArgs ea, string payload)
        {
            if (ea.ApplicationMessage.Topic == _mqttTopicPing)
                await OnTopicPing(ea, payload);

            else if (ea.ApplicationMessage.Topic == _mqttTopicSettings)
                OnTopicSettings(ea, payload);

            else if (ea.ApplicationMessage.Topic == _mqttTopicChangestate)
                OnTopicChangestate(ea, payload);

            else
                _logger.Debug($"Unknown topic {ea.ApplicationMessage.Topic}!");
        }

        internal async Task OnTopicPing(MqttApplicationMessageReceivedEventArgs ea, string payload)
        {
            await PublishBeacon();
        }

        internal void OnTopicSettings(MqttApplicationMessageReceivedEventArgs ea, string payload)
        {
            ActionSettings? actionSettings = JsonConvert.DeserializeObject<ActionSettings>(payload);
            if (actionSettings != null)
                _mqttClientSettings.CollectionInterval = actionSettings.CollectionInterval;
            else
                _logger.Debug($"Recieved no sensor data on topic {ea.ApplicationMessage.Topic}!");
        }

        internal void OnTopicChangestate(MqttApplicationMessageReceivedEventArgs ea, string payload)
        {
            ActionChangeState? actionChangeState = JsonConvert.DeserializeObject<ActionChangeState>(payload);
            if (actionChangeState != null)
                _mqttClientSettings.State = actionChangeState.Value;
            else
                _logger.Debug($"Recieved no sensor data on topic {ea.ApplicationMessage.Topic}!");
        }

        private async Task PublishBeacon()
        {
            DeviceBeacon deviceBeaconModel = new DeviceBeacon { Id = _clientId, Type = _clientType, State = _mqttClientSettings.State, CollectionInterval = _mqttClientSettings.CollectionInterval };

            _logger.Debug($"Publishing Beacon");
            var applicationMessage = new MqttApplicationMessageBuilder()
                .WithTopic(_mqttTopicBeacon)
                .WithPayload(JsonConvert.SerializeObject(deviceBeaconModel))
                .WithQualityOfServiceLevel(MQTTnet.Protocol.MqttQualityOfServiceLevel.AtMostOnce)
                .Build();

            await PublishMessage(applicationMessage);
        }

        internal List<MqttClientSubscribeOptions> GetSubScriptionOptions()
        {
            List<MqttClientSubscribeOptions> subscribeOptions = new List<MqttClientSubscribeOptions>();

            var topicFilter = new MqttTopicFilterBuilder()
                .WithTopic(_mqttTopicPing)
                .WithAtLeastOnceQoS()
                .Build();

            var subscribeOption = _mqttFactory.CreateSubscribeOptionsBuilder()
                .WithTopicFilter(topicFilter)
                .Build();

            subscribeOptions.Add(subscribeOption);

            topicFilter = new MqttTopicFilterBuilder()
                .WithTopic(_mqttTopicPing)
                .WithAtLeastOnceQoS()
                .Build();

            subscribeOption = _mqttFactory.CreateSubscribeOptionsBuilder()
                .WithTopicFilter(topicFilter)
                .Build();

            subscribeOptions.Add(subscribeOption);

            return subscribeOptions;
        }

        private async Task SimulateDevice()
        {
            while (!_cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(_mqttClientSettings.CollectionInterval), _cancellationToken);
                if (_mqttClientSettings.DeviceType == "Weather")
                {
                    Sensor sensor = new Sensor { Id = _mqttClientSettings.ClientId, Type = _mqttClientSettings.DeviceType };
                    var (temp, hum) = TemperatureHumiditySimulation.GenerateSimulation(DateTime.Now.Hour);
                    sensor.Attributes.Add(new SensorAttribute { Name = "Temperature", Value = temp.ToString() });
                    sensor.Attributes.Add(new SensorAttribute { Name = "Temperature", Value = hum.ToString() });
                    var applicationMessage = new MqttApplicationMessageBuilder()
                    .WithTopic(_mqttTelemetry)
                    .WithPayload(JsonConvert.SerializeObject(sensor))
                    .WithQualityOfServiceLevel(MQTTnet.Protocol.MqttQualityOfServiceLevel.AtMostOnce)
                    .Build();

                    await PublishMessage(applicationMessage);
                }

                else if (_mqttClientSettings.DeviceType == "Generator")
                {

                }                
                else if (_mqttClientSettings.DeviceType == "PowerDelivery")
                {

                }
            }
        }

        ~MqttService()
        {
            // Dispose of the MQTT client manually at the end
            if (_mqttClient.IsConnected)
            {
                _mqttClient.DisconnectAsync().Wait();
            }   

            _mqttClient.Dispose();
        }
    }
}
