using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Device_Simulator.Models
{
    public class Sensor
    {
        public required string Id { get; set; }
        public required string Type { get; set; }

        public List<SensorAttribute> Attributes { get; set; } = new List<SensorAttribute>();

    }

    public class SensorAttribute
    {
        public required string Name { get; set; }
        public required string Value { get; set; }
    }
}
