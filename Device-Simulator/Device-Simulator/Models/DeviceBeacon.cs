using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Device_Simulator.Models
{
    public class DeviceBeacon
    {
        public required string Id { get; set; }
        public required string Type { get; set; } = "Sensor";
        public int CollectionInterval { get; set; }
        public required string State { get; set; }
    }
}
