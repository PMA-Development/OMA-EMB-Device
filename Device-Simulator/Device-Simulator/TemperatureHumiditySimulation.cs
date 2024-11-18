using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Device_Simulator
{
    public static class TemperatureHumiditySimulation
    {
        private static Random _rand = new Random();

        public static (double temperature, double humidity) GenerateSimulation(int hourOfDay)
        {
            // Simulating a daily cycle using a sine wave for temperature (with random variation)
            double temperature = 20 + 10 * Math.Sin((hourOfDay / 24.0) * 2 * Math.PI) + GetRandomVariation();

            // Simulating daily humidity cycle (e.g., higher in the morning and evening)
            double humidity = 50 + 30 * Math.Cos((hourOfDay / 24.0) * 2 * Math.PI) + GetRandomVariation();

            // Clamping values to realistic ranges
            temperature = Math.Max(-10, Math.Min(50, temperature));  // Temperature range -10°C to 50°C
            humidity = Math.Max(0, Math.Min(100, humidity));  // Humidity range 0% to 100%

            return (temperature, humidity);
        }

        // Add small random variation to make it more realistic
        private static double GetRandomVariation()
        {
            return _rand.NextDouble() * 2 - 1;  // Random variation between -1 and 1
        }
    }

}
