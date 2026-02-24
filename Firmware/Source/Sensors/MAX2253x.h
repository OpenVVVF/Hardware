// ========================= MAX2253x.h =========================
#pragma once

#include <cstdint>
#include <array>
#include <vector>

#ifndef UNIT_TEST
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/structs/sio.h"
#include "hardware/dma.h"
#endif

enum class ErrorCode {
    NONE = 0,
    NOT_INITIALIZED,
    WRONG_DEVICE_ID,
    ADC_FUNCTIONAL_FAULT,
    FIELD_DATA_LOSS,
    SPI_FRAMING_ERROR,
    SPI_CRC_ERROR,
    ADC_ALL_ZEROS,
    ADC_ALL_MAX,
    TIMEOUT
};

class MAX2253x_Device {
public:
    explicit MAX2253x_Device(uint8_t cs_pin);
    void read_all_adc_raw_into(uint16_t out4[4], uint16_t* int_status = nullptr, bool use_filtered = false);
    void read_all_adc_voltage_into(float out4[4], uint16_t* int_status = nullptr);
    bool init();
    bool is_initialized() const { return m_initialized; }
    
    bool verify_chip_id();
    bool check_diagnostics();
    bool verify_adc_reading();
    
    std::array<uint16_t, 4> read_all_adc_raw();
    std::array<float, 4> read_all_adc_voltage();
    
    uint8_t get_cs_pin() const { return m_cs_pin; }
    ErrorCode get_last_error() const { return m_last_error; }
    const char* get_error_string() const;
    uint8_t get_device_id() const { return m_device_id; }

private:
    uint32_t m_cs_mask;
    uint8_t m_cs_pin;
    bool m_initialized;
    uint8_t m_device_id;
    ErrorCode m_last_error;
    
    void begin_transaction();
    void end_transaction();
    uint16_t read_register(uint8_t address);
    
    // Allow MultiADC to access private members for initialization/diagnostics
    friend class MAX2253x_MultiADC;
};

class MAX2253x_MultiADC {
public:
    static spi_inst_t* SPI_PORT;
    static constexpr uint32_t SPI_BAUDRATE = 10'000'000;
    
    explicit MAX2253x_MultiADC(const std::vector<uint8_t>& cs_pins);
    void read_device_voltage_into(size_t index, float out4[4]);
    const std::vector<std::array<uint16_t, 4>>& read_all_devices_raw_ref();
    const std::vector<std::array<float, 4>>& read_all_devices_voltage_ref();

    void read_all_devices_raw_into(std::array<uint16_t, 4>* out, size_t out_count);
    void read_all_devices_voltage_into(std::array<float, 4>* out, size_t out_count);
    

    bool init();
    void print_status();
    std::array<uint16_t, 4> read_device_raw(size_t device_index);
    std::array<float, 4> read_device_voltage(size_t index);
    std::vector<std::array<uint16_t, 4>> read_all_devices_raw();
    std::vector<std::array<float, 4>> read_all_devices_voltage();
    size_t get_device_count() const { return m_devices.size(); }

    // =========================================================================
    // --- ASYNCHRONOUS DMA API ---
    // =========================================================================
    
    void init_dma();
    void start_async_read();
    
    // Checked by the FOC loop to see if the DMA sequence has finished
    bool is_async_ready() const { return m_async_ready; }
    
    // Converts the raw DMA byte buffers into usable floating-point voltages
    void process_async_data(); 
    
    // Called by the hardware DMA interrupt.
    void dma_isr(); 
    
    void set_filtered_read(bool enable);

    static MAX2253x_MultiADC* instance;

    // Fast inline accessor for MeasurementSystem to grab the parsed data
    const float* get_cached_device_voltage(size_t index) const {
        return m_voltage_cache[index].data();
    }

private:
    std::vector<MAX2253x_Device> m_devices;
    std::vector<std::array<uint16_t, 4>> m_raw_cache;
    std::vector<std::array<float, 4>> m_voltage_cache;

    // --- DMA State Tracking ---
    uint m_dma_tx;
    uint m_dma_rx;
    volatile size_t m_async_current_dev;
    volatile bool m_async_busy = false;
    volatile bool m_async_ready = false;
    
    static uint8_t TX_BUFFER[11];
    std::vector<std::array<uint8_t, 11>> m_async_rx_buffers;
};