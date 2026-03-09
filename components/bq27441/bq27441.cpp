#include "bq27441.h"
#include "bq27441_definitions.h"

#include <array>

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome::bq27441
{
    static const char *const TAG = "bq27441";

    void BQ27441::setup()
    {
        // The setup is async in loop()
        _initialization = Initialization::Initializing;
        loop();
    }

    void BQ27441::loop()
    {
        switch (_initialization)
        {
        case Initialization::Initializing:
        {
            ESP_LOGD(TAG, "Initializing");
            optional<uint16_t> device_id = this->read_control_word(BQ27441_CONTROL_DEVICE_TYPE);
            if (!device_id.has_value())
                return this->mark_failed(LOG_STR("Failed to read the device ID"));

            if (device_id.value() != BQ27441_DEVICE_ID)
                return this->mark_failed(LOG_STR("Wrong device ID"));

            optional<uint16_t> fw_version = this->read_control_word(BQ27441_CONTROL_FW_VERSION);
            if (!fw_version.has_value())
                ESP_LOGW(TAG, "Failed to read the firmware version");
            else
                ESP_LOGD(TAG, "Firmware version: %#02x", fw_version.value());

            if (!_design_capacity.has_value() && !_design_energy.has_value())
            {
                // No conf, skip configuration
                ESP_LOGD(TAG, "Initialized");
                _initialization = Initialization::Initialized;
                return;
            }

            ESP_LOGV(TAG, "Unsealing configuration");

            // Unsealing is done by writing twice the unsealing key
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_UNSEAL_KEY);
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_UNSEAL_KEY);

            optional<uint16_t> status = this->read_control_word(BQ27441_CONTROL_STATUS);
            if (!status.has_value() || (status.value() & BQ27441_STATUS_SS))
                return this->mark_failed(LOG_STR("Failed to unseal configuration"));

            ESP_LOGV(TAG, "Entering config update");
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_SET_CFGUPDATE);

            _initialization = Initialization::WriteConfig;
            _initialization_retry = 0;
        }
        case Initialization::WriteConfig:
        {
            ESP_LOGV(TAG, "Waiting for config mode set %d/32", _initialization_retry + 1);

            optional<uint16_t> flags_value = this->read_u16(BQ27441_COMMAND_FLAGS);
            if (!flags_value.has_value() || !(flags_value.value() & BQ27441_FLAG_CFGUPMODE))
            {
                _initialization_retry++;
                if (_initialization_retry >= 32)
                    this->mark_failed(LOG_STR("Failed to enter config update"));
                return;
            }

            ESP_LOGV(TAG, "Setup block RAM update");
            this->write_byte(BQ27441_EXTENDED_CONTROL, 0x00);
            this->write_byte(BQ27441_EXTENDED_DATACLASS, BQ27441_ID_STATE);
            this->write_byte(BQ27441_EXTENDED_DATABLOCK, 0x00);

            // Read the previous data to update the checksum
            ESP_LOGV(TAG, "Read previous block RAM data");
            optional<std::array<uint8_t, 32>> old_data = this->read_bytes<32>(BQ27441_EXTENDED_BLOCKDATA);
            if (!old_data.has_value())
                return this->mark_failed(LOG_STR("Failed to read RAM state before update"));

            optional<uint8_t> old_checksum = this->read_byte(BQ27441_EXTENDED_CHECKSUM);
            if (!old_checksum.has_value())
                return this->mark_failed(LOG_STR("Failed to read RAM checksum before update"));

            uint8_t tmp_checksum = 0xFF - old_checksum.value();

            // Write the new config
            if (_design_capacity.has_value())
            {
                write_extended_block_data(_design_capacity.value(), 10, &tmp_checksum);
            }

            if (_design_energy.has_value())
            {
                write_extended_block_data(_design_energy.value(), 12, &tmp_checksum);
            }

            // Write new checksum. This applies the RAM write if matching.
            uint8_t new_expected_checksum = 0xFF - tmp_checksum;
            this->write_byte(BQ27441_EXTENDED_CHECKSUM, new_expected_checksum);

            // Verify checksum
            this->write_byte(BQ27441_EXTENDED_DATACLASS, BQ27441_ID_STATE);
            this->write_byte(BQ27441_EXTENDED_DATABLOCK, 0x00);

            optional<uint8_t> new_checksum = this->read_byte(BQ27441_EXTENDED_CHECKSUM);
            if (!new_checksum.has_value() || new_checksum.value() != new_expected_checksum)
                return this->mark_failed(LOG_STR("Checksum error"));

            ESP_LOGV(TAG, "Performing soft reset");
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_SOFT_RESET);

            _initialization = Initialization::ExitConfig;
            _initialization_retry = 0;
        }
        case Initialization::ExitConfig:
        {
            ESP_LOGV(TAG, "Waiting for config mode unset %d/32", _initialization_retry + 1);

            optional<uint16_t> flags_value = this->read_u16(BQ27441_COMMAND_FLAGS);
            if (!flags_value.has_value() || (flags_value.value() & BQ27441_FLAG_CFGUPMODE))
            {
                _initialization_retry++;

                if (_initialization_retry >= 32)
                    this->mark_failed(LOG_STR("Failed to exit config update"));
                return;
            }

            ESP_LOGV(TAG, "Sealing the configuration");
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_SEALED);
            optional<uint16_t> status = read_control_word(BQ27441_CONTROL_STATUS);
            if (!status.has_value() || !(status.value() & BQ27441_STATUS_SS))
                return this->mark_failed(LOG_STR("Resealing did not work"));

            ESP_LOGD(TAG, "Initialized");
            _initialization = Initialization::Initialized;
        }
        }
    }

    void BQ27441::write_extended_block_data(uint16_t to_write, uint8_t offset, uint8_t *tmp_checksum)
    {
        uint8_t msb = to_write >> 8;
        uint8_t lsb = to_write & 0x00FF;
        std::array<uint8_t, 2> data{msb, lsb};
        for (uint8_t i = 0; i < data.size(); ++i)
        {
            // Update the checksum with the previous value
            uint8_t prev = this->read_byte(BQ27441_EXTENDED_BLOCKDATA + offset + i).value_or(0);
            *tmp_checksum -= prev;
            // Write to new value
            this->write_byte(BQ27441_EXTENDED_BLOCKDATA + offset + i, data[i]);
            // Update the checksum with the new value
            *tmp_checksum += data[i];
        }
    }

    void BQ27441::dump_config()
    {
        ESP_LOGCONFIG(TAG, "BQ27441:");
        LOG_I2C_DEVICE(this);
        LOG_UPDATE_INTERVAL(this);

        if (_design_capacity.has_value())
        {
            ESP_LOGCONFIG(TAG, "  Design Capacity: %d", _design_capacity.value());
        }

        if (_design_energy.has_value())
        {
            ESP_LOGCONFIG(TAG, "  Design Energy: %d", _design_energy.value());
        }

        LOG_SENSOR("  ", "Level", _level);
        LOG_SENSOR("  ", "Voltage", _voltage);
        LOG_SENSOR("  ", "Remaining Capacity", _remaining_capacity);
        LOG_SENSOR("  ", "Temperature", _temperature);
        LOG_SENSOR("  ", "Power", _power);
        LOG_SENSOR("  ", "Current", _current);
        LOG_SENSOR("  ", "Health", _health);
    }

    void BQ27441::update()
    {
        if (_initialization != Initialization::Initialized)
            return;

        if (_level)
        {
            optional<uint16_t> data = read_u16(BQ27441_COMMAND_SOC);
            if (data.has_value())
                _level->publish_state((float)data.value());
        }

        if (_voltage)
        {
            optional<uint16_t> data = read_u16(BQ27441_COMMAND_VOLTAGE);
            if (data.has_value())
                _voltage->publish_state((float)data.value());
        }

        if (_remaining_capacity)
        {
            optional<uint16_t> data = read_u16(BQ27441_COMMAND_REM_CAPACITY);
            if (data.has_value())
                _remaining_capacity->publish_state((float)data.value());
        }

        if (_temperature)
        {
            optional<uint16_t> data = read_u16(BQ27441_COMMAND_TEMP);
            // It's in 0.1K, convert to celcius
            if (data.has_value())
                _temperature->publish_state(-273.15 + 0.1 * (float)data.value());
        }

        if (_power)
        {
            optional<int16_t> data = read_i16(BQ27441_COMMAND_AVG_POWER);
            // It's in milliWh, convert to Wh
            if (data.has_value())
                _power->publish_state(0.001 * (float)data.value());
        }

        if (_current)
        {
            optional<int16_t> data = read_i16(BQ27441_COMMAND_AVG_CURRENT);
            if (data.has_value())
                _current->publish_state((float)data.value());
        }

        if (_health)
        {
            optional<uint16_t> data = this->read_u16(BQ27441_COMMAND_SOH);
            if (data.has_value())
            {
                uint8_t soh_percent = data.value() & 0x00FF;
                _health->publish_state((float)soh_percent);
            }
        }
    }

    optional<std::uint16_t> BQ27441::read_control_word(uint16_t function)
    {
        this->write_u16(BQ27441_COMMAND_CONTROL, function);
        return this->read_u16(BQ27441_COMMAND_CONTROL);
    }

    bool BQ27441::write_u16(uint8_t a_register, uint16_t data)
    {
        uint8_t msb = (data >> 8);
        uint8_t lsb = (data & 0x00FF);
        uint8_t data2[2] = {lsb, msb};
        return this->write_bytes(a_register, data2, 2);
    }

    optional<uint16_t> BQ27441::read_u16(uint8_t a_register)
    {
        optional<std::array<uint8_t, 2>> data2 = this->read_bytes<2>(a_register);
        if (!data2.has_value())
            return nullopt;
        return ((uint16_t)data2.value()[1] << 8) | data2.value()[0];
    }
    optional<int16_t> BQ27441::read_i16(uint8_t a_register)
    {
        optional<uint16_t> data = this->read_u16(a_register);
        if (!data.has_value())
            return nullopt;
        return (int16_t)data.value();
    }
}
