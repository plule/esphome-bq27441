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
        SUB_SENSOR(level)
        SUB_SENSOR(voltage)
        SUB_SENSOR(remaining_capacity)
        SUB_SENSOR(temperature)
        SUB_SENSOR(power)
        SUB_SENSOR(current)
        SUB_SENSOR(health)

    public:
        // Component API
        void setup() override;
        void loop() override;
        void dump_config() override;
        void update() override;

        // Configurations assignation
        void set_capacity(uint16_t value) { design_capacity_ = value; }
        void set_design_energy(uint16_t value) { design_energy_ = value; }

    protected:
        bool write_u16(uint8_t a_register, uint16_t data);
        optional<uint16_t> read_u16(uint8_t a_register);
        optional<int16_t> read_i16(uint8_t a_register);
        optional<uint16_t> read_control_word(uint16_t function);
        bool write_extended_block_data(uint16_t to_write, uint8_t offset, uint8_t *tmp_checksum);

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
        } initialization_state_;

        /// @brief Initialization retry counter
        uint8_t initialization_retry_ = 0;

        /// @brief Nominal capacity of the battery
        optional<uint16_t> design_capacity_;
        /// @brief Nominal energy of the battery
        optional<uint16_t> design_energy_;
    };
}
