#pragma once

#include <optional>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

#include "bq27441_definitions.h"

namespace esphome::bq27441
{
    class BQ27441 : public PollingComponent, public i2c::I2CDevice
    {
    public:
        // Component API
        void setup() override;
        void loop() override;
        void dump_config() override;
        void update() override;

        // Configurations assignation
        void set_capacity(uint16_t value) { _design_capacity = value; }
        void set_design_energy(uint16_t value) { _design_energy = value; }

        // Sensor assignation
        void set_level_sensor(sensor::Sensor *sensor) { _level = sensor; }
        void set_voltage_sensor(sensor::Sensor *sensor) { _voltage = sensor; }
        void set_remaining_capacity_sensor(sensor::Sensor *sensor) { _remaining_capacity = sensor; }
        void set_temperature_sensor(sensor::Sensor *sensor) { _temperature = sensor; }
        void set_power_sensor(sensor::Sensor *sensor) { _power = sensor; }
        void set_current_sensor(sensor::Sensor *sensor) { _current = sensor; }
        void set_health_sensor(sensor::Sensor *sensor) { _health = sensor; }

    protected:
        /// @brief Battery level in %
        sensor::Sensor *_level{nullptr};
        /// @brief Battery voltage in mV
        sensor::Sensor *_voltage{nullptr};
        /// @brief Remaining capacity in mAh
        sensor::Sensor *_remaining_capacity{nullptr};
        /// @brief Temperature in celcius
        sensor::Sensor *_temperature{nullptr};
        /// @brief Battery power draw (< 0: Discharging, > 0 Charging, == 0 Neither)
        sensor::Sensor *_power{nullptr};
        /// @brief Battery current draw (< 0: Discharging, > 0 Charging, == 0 Neither)
        sensor::Sensor *_current{nullptr};
        /// @brief Battery estimated health in %
        sensor::Sensor *_health = nullptr;

        bool write_u16(uint8_t a_register, uint16_t data);
        optional<uint16_t> read_u16(uint8_t a_register);
        optional<int16_t> read_i16(uint8_t a_register);
        optional<uint16_t> read_control_word(uint16_t function);
        void write_extended_block_data(uint16_t to_write, uint8_t offset, uint8_t *tmp_checksum);

        /// @brief Configuration data to write
        struct ExtendedDataConfig
        {
            ExtendedDataConfig(const char *name, uint8_t offset)
                : name(name), offset(offset) {}
            const char *name;
            uint8_t offset;
            optional<uint16_t> to_write;
        };

        /// @brief Initialization state machine status
        enum class Initialization
        {
            Initializing,
            WriteConfig,
            ExitConfig,
            Initialized,
        } _initialization;

        /// @brief Initialization retry counter
        uint8_t _initialization_retry = 0;

        /// @brief Nominal capacity of the battery
        optional<uint16_t> _design_capacity;
        /// @brief Nominal energy of the battery
        optional<uint16_t> _design_energy;
    };
}
