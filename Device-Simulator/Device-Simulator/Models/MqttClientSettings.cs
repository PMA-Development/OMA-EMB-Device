using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Device_Simulator.Models
{
    public class MqttClientSettings
    {
        public required string ClientId { get; set; }
        public required string Username { get; set; }
        public required string Password { get; set; }
        public bool UseTLS { get; set; }
        public required string Host { get; set; }
        public required int Port { get; set; } = 1883;
        public string State { get; set; } = "On";
        public int CollectionInterval { get; set; } = 5;
        public required string DeviceType { get; set; }
    }
}
