#include "bq27441.h"
#include "bq27441_definitions.h"

#include <array>

#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::bq27441
{
    static const char *const TAG = "bq27441";
    static const size_t MAX_STEP_RETRY = 128;

    void BQ27441::setup()
    {
        // The setup is async in loop()
        this->stop_poller();
        this->initialization_state_ = Initialization::Initializing;
        this->loop();
    }

    void BQ27441::loop()
    {
        switch (this->initialization_state_)
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

            ESP_LOGV(TAG, "Unsealing configuration");

            // Unsealing is done by writing twice the unsealing key
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_UNSEAL_KEY);
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_UNSEAL_KEY);

            optional<uint16_t> status = this->read_control_word(BQ27441_CONTROL_STATUS);
            if (!status.has_value() || (status.value() & BQ27441_STATUS_SS))
                return this->mark_failed(LOG_STR("Failed to unseal configuration"));

            ESP_LOGV(TAG, "Entering config update");
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_SET_CFGUPDATE);

            this->initialization_state_ = Initialization::WriteConfig;
            this->initialization_retry_ = 0;
        }
        case Initialization::WriteConfig:
        {
            ESP_LOGV(TAG, "Waiting for config mode set %d/%d", initialization_retry_ + 1, MAX_STEP_RETRY);

            optional<uint16_t> flags_value = this->read_u16(BQ27441_COMMAND_FLAGS);
            if (!flags_value.has_value() || !(flags_value.value() & BQ27441_FLAG_CFGUPMODE))
            {
                initialization_retry_++;
                if (initialization_retry_ >= MAX_STEP_RETRY)
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
            bool changed = false;
            if (this->design_capacity_.has_value())
            {
                changed |= this->write_extended_block_data(design_capacity_.value(), 10, &tmp_checksum);
            }

            if (this->design_energy_.has_value())
            {
                changed |= this->write_extended_block_data(design_energy_.value(), 12, &tmp_checksum);
            }

            if (changed)
            {
                // Write new checksum. This applies the RAM write if matching.
                uint8_t new_expected_checksum = 0xFF - tmp_checksum;
                this->write_byte(BQ27441_EXTENDED_CHECKSUM, new_expected_checksum);

                // Verify checksum
                this->write_byte(BQ27441_EXTENDED_DATACLASS, BQ27441_ID_STATE);
                this->write_byte(BQ27441_EXTENDED_DATABLOCK, 0x00);

                optional<uint8_t> new_checksum = this->read_byte(BQ27441_EXTENDED_CHECKSUM);
                if (!new_checksum.has_value() || new_checksum.value() != new_expected_checksum)
                    return this->mark_failed(LOG_STR("Checksum error"));
            }

            // We must reset if the ITPOR flag is set (RAM was cleared, powerloss), or if any update
            // to the ram config is actually done
            bool itpor = flags_value.value() & BQ27441_FLAG_ITPOR;
            if (changed || itpor)
            {
                ESP_LOGD(TAG, "Performing soft reset after configuration update");
                this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_SOFT_RESET);
            }
            else
            {
                ESP_LOGD(TAG, "Skipping soft reset");
                this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_EXIT_CFGUPDATE);
            }

            this->initialization_state_ = Initialization::ExitConfig;
            this->initialization_retry_ = 0;
        }
        case Initialization::ExitConfig:
        {
            ESP_LOGV(TAG, "Waiting for config mode unset %d/%d", initialization_retry_ + 1, MAX_STEP_RETRY);

            optional<uint16_t> flags_value = this->read_u16(BQ27441_COMMAND_FLAGS);
            if (!flags_value.has_value() || (flags_value.value() & BQ27441_FLAG_CFGUPMODE))
            {
                this->initialization_retry_++;

                if (this->initialization_retry_ >= MAX_STEP_RETRY)
                    this->mark_failed(LOG_STR("Failed to exit config update"));
                return;
            }

            ESP_LOGV(TAG, "Sealing the configuration");
            this->write_u16(BQ27441_COMMAND_CONTROL, BQ27441_CONTROL_SEALED);
            optional<uint16_t> status = this->read_control_word(BQ27441_CONTROL_STATUS);
            if (!status.has_value() || !(status.value() & BQ27441_STATUS_SS))
                return this->mark_failed(LOG_STR("Resealing did not work"));

            ESP_LOGD(TAG, "Initialized");
            this->initialization_state_ = Initialization::Initialized;
            this->start_poller();
        }
        }
    }

    void BQ27441::dump_config()
    {
        ESP_LOGCONFIG(TAG, "BQ27441:");
        LOG_I2C_DEVICE(this);
        LOG_UPDATE_INTERVAL(this);

        if (this->design_capacity_.has_value())
        {
            ESP_LOGCONFIG(TAG, "  Design Capacity: %d", design_capacity_.value());
        }

        if (this->design_energy_.has_value())
        {
            ESP_LOGCONFIG(TAG, "  Design Energy: %d", design_energy_.value());
        }

        LOG_SENSOR("  ", "Level", this->level_sensor_);
        LOG_SENSOR("  ", "Voltage", this->voltage_sensor_);
        LOG_SENSOR("  ", "Remaining Capacity", this->remaining_capacity_sensor_);
        LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
        LOG_SENSOR("  ", "Power", this->power_sensor_);
        LOG_SENSOR("  ", "Current", this->current_sensor_);
        LOG_SENSOR("  ", "Health", this->health_sensor_);
    }

    void BQ27441::update()
    {
        if (this->initialization_state_ != Initialization::Initialized)
            return;

        if (this->level_sensor_)
        {
            optional<uint16_t> data = this->read_u16(BQ27441_COMMAND_SOC);
            if (data.has_value())
                this->level_sensor_->publish_state((float)data.value());
        }

        if (this->voltage_sensor_)
        {
            optional<uint16_t> data = this->read_u16(BQ27441_COMMAND_VOLTAGE);
            if (data.has_value())
                this->voltage_sensor_->publish_state((float)data.value());
        }

        if (this->remaining_capacity_sensor_)
        {
            optional<uint16_t> data = this->read_u16(BQ27441_COMMAND_REM_CAPACITY);
            if (data.has_value())
                this->remaining_capacity_sensor_->publish_state((float)data.value());
        }

        if (this->temperature_sensor_)
        {
            optional<uint16_t> data = this->read_u16(BQ27441_COMMAND_TEMP);
            // It's in 0.1K, convert to celcius
            if (data.has_value())
                this->temperature_sensor_->publish_state(-273.15 + 0.1 * (float)data.value());
        }

        if (this->power_sensor_)
        {
            optional<int16_t> data = this->read_i16(BQ27441_COMMAND_AVG_POWER);
            // It's in milliWh, convert to Wh
            if (data.has_value())
                this->power_sensor_->publish_state(0.001 * (float)data.value());
        }

        if (this->current_sensor_)
        {
            optional<int16_t> data = this->read_i16(BQ27441_COMMAND_AVG_CURRENT);
            if (data.has_value())
                this->current_sensor_->publish_state((float)data.value());
        }

        if (this->health_sensor_)
        {
            optional<uint16_t> data = this->read_u16(BQ27441_COMMAND_SOH);
            if (data.has_value())
            {
                uint8_t soh_percent = data.value() & 0x00FF;
                this->health_sensor_->publish_state((float)soh_percent);
            }
        }
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

    optional<std::uint16_t> BQ27441::read_control_word(uint16_t function)
    {
        this->write_u16(BQ27441_COMMAND_CONTROL, function);
        return this->read_u16(BQ27441_COMMAND_CONTROL);
    }

    bool BQ27441::write_extended_block_data(uint16_t to_write, uint8_t offset, uint8_t *tmp_checksum)
    {
        uint8_t msb = to_write >> 8;
        uint8_t lsb = to_write & 0x00FF;
        std::array<uint8_t, 2> data{msb, lsb};
        bool changed = false;
        for (uint8_t i = 0; i < data.size(); ++i)
        {
            uint8_t address = BQ27441_EXTENDED_BLOCKDATA + offset + i;
            uint8_t prev = this->read_byte(address).value_or(0);
            if (prev != data[i])
            {
                // Update the checksum removing the previous value
                changed = true;
                *tmp_checksum -= prev;
                // Write to new value
                this->write_byte(address, data[i]);
                // Update the checksum with the new value
                *tmp_checksum += data[i];
            }
        }
        return changed;
    }
}
