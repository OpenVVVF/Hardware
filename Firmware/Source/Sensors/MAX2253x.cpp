// ========================= MAX2253x.cpp =========================
#include "MAX2253x.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <cstdio>
#include <cstring>
#include "Hardware.h"

#include "hardware/sync.h"

static constexpr float VOLTAGE_REFERENCE = 1.8f;
static constexpr uint16_t ADC_MAX_VALUE = 4095;

static constexpr uint8_t REG_PROD_ID = 0x00;
static constexpr uint8_t REG_INTERRUPT_STATUS = 0x12;
static constexpr uint8_t REG_CONTROL = 0x14;
static constexpr uint16_t DEVICE_ID_EXPECTED = 0x0000;

spi_inst_t* MAX2253x_MultiADC::SPI_PORT = spi1;

// Global TX dummy buffer to push 11 bytes (Header 0x05 + 10 zero bytes)
uint8_t MAX2253x_MultiADC::TX_BUFFER[11] = { 0x05, 0,0,0,0,0,0,0,0,0,0 };
MAX2253x_MultiADC* MAX2253x_MultiADC::instance = nullptr;

namespace {
    // Static C-style ISR wrapper
    void max2253x_dma_isr_wrapper() {
        if (MAX2253x_MultiADC::instance) {
            MAX2253x_MultiADC::instance->dma_isr();
        }
    }
}

// Helper function for SPI transaction with timeout
static bool spi_read_with_timeout(spi_inst_t* spi, uint8_t* rx_buffer, size_t len, uint32_t timeout_us) {
    absolute_time_t timeout = make_timeout_time_us(timeout_us);
    size_t bytes_read = 0;
    while (bytes_read < len && !time_reached(timeout)) {
        if (spi_is_readable(spi)) {
            rx_buffer[bytes_read++] = spi_get_hw(spi)->dr;
        }
    }
    return bytes_read == len;
}

MAX2253x_Device::MAX2253x_Device(uint8_t cs_pin)
  : m_cs_pin(cs_pin),
    m_initialized(false),
    m_device_id(0),
    m_last_error(ErrorCode::NONE),
    m_cs_mask(1u << cs_pin) {}

static inline void cs_low(uint32_t mask)  { sio_hw->gpio_clr = mask; }
static inline void cs_high(uint32_t mask) { sio_hw->gpio_set = mask; }

void MAX2253x_Device::begin_transaction() { cs_low(m_cs_mask); }
void MAX2253x_Device::end_transaction()   { cs_high(m_cs_mask); }

bool MAX2253x_Device::init() {
    gpio_init(m_cs_pin);
    gpio_set_dir(m_cs_pin, GPIO_OUT);
    gpio_put(m_cs_pin, 1);
    
    m_initialized = true;
    printf("MAX2253x: Device CS=GPIO%u initialized\n", m_cs_pin);
    return true;
}

uint16_t MAX2253x_Device::read_register(uint8_t address) {
    uint8_t header = (address & 0x3F) << 2;
    begin_transaction();
    spi_write_blocking(MAX2253x_MultiADC::SPI_PORT, &header, 1);
    uint8_t rx[2];
    spi_read_blocking(MAX2253x_MultiADC::SPI_PORT, 0, rx, 2);
    end_transaction();
    return ((rx[0] << 8) | rx[1]) & 0x0FFF;
}

std::array<uint16_t, 4> MAX2253x_Device::read_all_adc_raw() {
    std::array<uint16_t, 4> values{};
    read_all_adc_raw_into(values.data(), nullptr);
    return values;
}

std::array<float, 4> MAX2253x_Device::read_all_adc_voltage() {
    std::array<float, 4> voltages{};
    float tmp[4];
    read_all_adc_voltage_into(tmp, nullptr);
    voltages[0] = tmp[0]; voltages[1] = tmp[1]; voltages[2] = tmp[2]; voltages[3] = tmp[3];
    return voltages;
}

bool MAX2253x_Device::verify_chip_id() {
    if (!m_initialized) {
        m_last_error = ErrorCode::NOT_INITIALIZED;
        return false;
    }
    uint16_t prod_id = read_register(REG_PROD_ID);
    uint8_t device_id = (prod_id >> 8) & 0xFF;
    bool por_bit = prod_id & 0x80;
    
    printf("  Device CS=GPIO%u: PROD_ID=0x%04X (DEVICE_ID=0x%02X, POR=%d)\n", 
           m_cs_pin, prod_id, device_id, por_bit);
    
    if (device_id != DEVICE_ID_EXPECTED) {
        m_last_error = ErrorCode::WRONG_DEVICE_ID;
        printf("  ERROR: Wrong device ID! Expected 0x%02X, got 0x%02X\n", 
               DEVICE_ID_EXPECTED, device_id);
        return false;
    }
    if (!por_bit) {
        printf("  WARNING: POR bit not set, device may not have powered on properly\n");
    }
    m_device_id = device_id;
    return true;
}

bool MAX2253x_Device::check_diagnostics() {
    if (!m_initialized) {
        m_last_error = ErrorCode::NOT_INITIALIZED;
        return false;
    }
    uint16_t int_status = read_register(REG_INTERRUPT_STATUS);
    printf("  Device CS=GPIO%u: INTERRUPT_STATUS=0x%04X\n", m_cs_pin, int_status);
    bool has_fault = false;
    
    if (int_status & 0x0800) {
        printf("  ERROR: ADC functionality error detected!\n");
        has_fault = true;
        m_last_error = ErrorCode::ADC_FUNCTIONAL_FAULT;
    }
    if (int_status & 0x0400) {
        printf("  ERROR: Field-side data loss detected!\n");
        has_fault = true;
        m_last_error = ErrorCode::FIELD_DATA_LOSS;
    }
    if (int_status & 0x0200) {
        printf("  ERROR: SPI framing error detected!\n");
        has_fault = true;
        m_last_error = ErrorCode::SPI_FRAMING_ERROR;
    }
    if (int_status & 0x0100) {
        printf("  ERROR: SPI CRC error detected!\n");
        has_fault = true;
        m_last_error = ErrorCode::SPI_CRC_ERROR;
    }
    return !has_fault;
}

void MAX2253x_Device::read_all_adc_raw_into(uint16_t out4[4], uint16_t* int_status, bool use_filtered) {
    // Create a dynamic buffer so we can inject the correct header
    uint8_t tx[11] = { 0,0,0,0,0,0,0,0,0,0,0 };
    tx[0] = use_filtered ? 0x15 : 0x05; 
    uint8_t rx[11];

    begin_transaction();
    uint32_t irq_status = save_and_disable_interrupts();
    spi_write_read_blocking(MAX2253x_MultiADC::SPI_PORT, tx, rx, 11);
    restore_interrupts(irq_status);
    end_transaction();

    out4[0] = ((rx[1] << 8) | rx[2]) & 0x0FFF;
    out4[1] = ((rx[3] << 8) | rx[4]) & 0x0FFF;
    out4[2] = ((rx[5] << 8) | rx[6]) & 0x0FFF;
    out4[3] = ((rx[7] << 8) | rx[8]) & 0x0FFF;

    if (int_status) *int_status = (uint16_t)((rx[9] << 8) | rx[10]);
}

void MAX2253x_Device::read_all_adc_voltage_into(float out4[4], uint16_t* int_status) {
    uint16_t raw4[4];
    read_all_adc_raw_into(raw4, int_status);
    const float scale = VOLTAGE_REFERENCE / static_cast<float>(ADC_MAX_VALUE);
    out4[0] = raw4[0] * scale;
    out4[1] = raw4[1] * scale;
    out4[2] = raw4[2] * scale;
    out4[3] = raw4[3] * scale;
}

bool MAX2253x_Device::verify_adc_reading() {
    if (!m_initialized) {
        m_last_error = ErrorCode::NOT_INITIALIZED;
        return false;
    }
    auto raw_values = read_all_adc_raw();
    printf("  Device CS=GPIO%u: ADC raw values: [", m_cs_pin);
    for (int i = 0; i < 4; i++) {
        printf("%u", raw_values[i]);
        if (i < 3) printf(", ");
    }
    printf("]\n");
    return true;
}

const char* MAX2253x_Device::get_error_string() const {
    switch (m_last_error) {
        case ErrorCode::NONE: return "No error";
        case ErrorCode::NOT_INITIALIZED: return "Not initialized";
        case ErrorCode::WRONG_DEVICE_ID: return "Wrong device ID";
        case ErrorCode::ADC_FUNCTIONAL_FAULT: return "ADC functional fault";
        case ErrorCode::FIELD_DATA_LOSS: return "Field-side data loss";
        case ErrorCode::SPI_FRAMING_ERROR: return "SPI framing error";
        case ErrorCode::SPI_CRC_ERROR: return "SPI CRC error";
        case ErrorCode::ADC_ALL_ZEROS: return "ADC all zeros";
        case ErrorCode::ADC_ALL_MAX: return "ADC all max values";
        default: return "Unknown error";
    }
}

MAX2253x_MultiADC::MAX2253x_MultiADC(const std::vector<uint8_t>& cs_pins) {
    m_devices.reserve(cs_pins.size());
    for(uint8_t cs : cs_pins) {
        m_devices.emplace_back(cs);
    }
    m_raw_cache.resize(m_devices.size());
    m_voltage_cache.resize(m_devices.size());
}

bool MAX2253x_MultiADC::init() {
    printf("\n=== MAX2253x Multi-ADC Smart Init ===\n");
    spi_init(SPI_PORT, SPI_BAUDRATE);
    uint actual = spi_init(SPI_PORT, SPI_BAUDRATE);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    printf("SPI requested: %u Hz, actual: %u Hz\n", SPI_BAUDRATE, actual);
    
    gpio_set_function(Hardware::Pins::SPI::SCK, GPIO_FUNC_SPI);
    gpio_set_function(Hardware::Pins::SPI::MOSI, GPIO_FUNC_SPI);
    gpio_set_function(Hardware::Pins::SPI::MISO, GPIO_FUNC_SPI);
    
    printf("SPI: %u Hz, SCK=%u, MOSI=%u, MISO=%u\n",
           SPI_BAUDRATE, Hardware::Pins::SPI::SCK, Hardware::Pins::SPI::MOSI, Hardware::Pins::SPI::MISO);
    
    bool all_ok = true;
    for(size_t i = 0; i < m_devices.size(); i++) {
        printf("\n--- Verifying ADC%zu (CS=GPIO%u) ---\n", i+1, m_devices[i].get_cs_pin());
        if(!m_devices[i].init()) {
            printf("ERROR: Failed to initialize ADC%zu\n", i+1);
            all_ok = false;
            continue;
        }
        sleep_ms(10);
        if(!m_devices[i].verify_chip_id()) {
            printf("ERROR: Chip ID verification failed for ADC%zu: %s\n", i+1, m_devices[i].get_error_string());
            all_ok = false;
            continue;
        }
        uint16_t status = m_devices[i].read_register(REG_INTERRUPT_STATUS);
        if (status != 0) {
            printf("  Cleared pending interrupt status: 0x%04X\n", status);
        }
        if(!m_devices[i].check_diagnostics()) {
            printf("ERROR: Diagnostic check failed for ADC%zu: %s\n", i+1, m_devices[i].get_error_string());
            all_ok = false;
            continue;
        }
        if(!m_devices[i].verify_adc_reading()) {
            printf("ERROR: ADC verification failed for ADC%zu: %s\n", i+1, m_devices[i].get_error_string());
            all_ok = false;
            continue;
        }
        printf("  ✓ ADC%zu verification PASSED\n", i+1);
    }
    
    if (all_ok) {
        printf("\n✓ All %zu devices verified and ready\n", m_devices.size());
        print_status();
        return true;
    } else {
        printf("\n✗ One or more devices failed verification\n");
        return false;
    }
}

void MAX2253x_MultiADC::print_status() {
    printf("\n=== MAX2253x System Status ===\n");
    printf("Device Count: %zu\n\n", m_devices.size());
    for(size_t i = 0; i < m_devices.size(); i++) {
        printf("ADC%zu: CS=GPIO%u - Status: %s\n", 
               i+1, m_devices[i].get_cs_pin(), 
               m_devices[i].get_error_string());
    }
    printf("===============================\n\n");
}

std::array<uint16_t, 4> MAX2253x_MultiADC::read_device_raw(size_t index) {
    if(index >= m_devices.size()) return {0, 0, 0, 0};
    return m_devices[index].read_all_adc_raw();
}

std::array<float, 4> MAX2253x_MultiADC::read_device_voltage(size_t index) {
    if(index >= m_devices.size()) return {0.0f, 0.0f, 0.0f, 0.0f};
    return m_devices[index].read_all_adc_voltage();
}

void MAX2253x_MultiADC::read_device_voltage_into(size_t index, float out4[4]) {
    if (index >= m_devices.size()) {
        out4[0] = out4[1] = out4[2] = out4[3] = 0.0f;
        return;
    }
    m_devices[index].read_all_adc_voltage_into(out4, nullptr);
}

const std::vector<std::array<uint16_t, 4>>& MAX2253x_MultiADC::read_all_devices_raw_ref() {
    for (size_t i = 0; i < m_devices.size(); i++) {
        m_devices[i].read_all_adc_raw_into(m_raw_cache[i].data(), nullptr);
    }
    return m_raw_cache;
}

const std::vector<std::array<float, 4>>& MAX2253x_MultiADC::read_all_devices_voltage_ref() {
    read_all_devices_raw_ref();
    const float scale = VOLTAGE_REFERENCE / static_cast<float>(ADC_MAX_VALUE);
    for (size_t i = 0; i < m_devices.size(); i++) {
        m_voltage_cache[i][0] = m_raw_cache[i][0] * scale;
        m_voltage_cache[i][1] = m_raw_cache[i][1] * scale;
        m_voltage_cache[i][2] = m_raw_cache[i][2] * scale;
        m_voltage_cache[i][3] = m_raw_cache[i][3] * scale;
    }
    return m_voltage_cache;
}

void MAX2253x_MultiADC::read_all_devices_raw_into(std::array<uint16_t, 4>* out, size_t out_count) {
    const size_t n = (out_count < m_devices.size()) ? out_count : m_devices.size();
    for (size_t i = 0; i < n; i++) {
        m_devices[i].read_all_adc_raw_into(out[i].data(), nullptr);
    }
}

void MAX2253x_MultiADC::read_all_devices_voltage_into(std::array<float, 4>* out, size_t out_count) {
    const size_t n = (out_count < m_devices.size()) ? out_count : m_devices.size();
    const float scale = VOLTAGE_REFERENCE / static_cast<float>(ADC_MAX_VALUE);
    for (size_t i = 0; i < n; i++) {
        uint16_t raw4[4];
        m_devices[i].read_all_adc_raw_into(raw4, nullptr);
        out[i][0] = raw4[0] * scale;
        out[i][1] = raw4[1] * scale;
        out[i][2] = raw4[2] * scale;
        out[i][3] = raw4[3] * scale;
    }
}

std::vector<std::array<float, 4>> MAX2253x_MultiADC::read_all_devices_voltage() {
    std::vector<std::array<float, 4>> results;
    results.resize(m_devices.size());
    read_all_devices_voltage_into(results.data(), results.size());
    return results;
}

std::vector<std::array<uint16_t, 4>> MAX2253x_MultiADC::read_all_devices_raw() {
    std::vector<std::array<uint16_t, 4>> results;
    results.resize(m_devices.size());
    read_all_devices_raw_into(results.data(), results.size());
    return results;
}

// ============================================================================
// --- DMA IMPLEMENTATION ---
// ============================================================================

void MAX2253x_MultiADC::set_filtered_read(bool enable) {
    // 0x05 = Burst Read Raw (Address 0x01)
    // 0x15 = Burst Read Filtered (Address 0x05)
    TX_BUFFER[0] = enable ? 0x15 : 0x05;
}

void MAX2253x_MultiADC::init_dma() {
    instance = this;
    m_async_rx_buffers.resize(m_devices.size());

    m_dma_tx = dma_claim_unused_channel(true);
    m_dma_rx = dma_claim_unused_channel(true);

    // TX Channel Config
    dma_channel_config c_tx = dma_channel_get_default_config(m_dma_tx);
    channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_8);
    channel_config_set_dreq(&c_tx, spi_get_dreq(SPI_PORT, true));
    channel_config_set_read_increment(&c_tx, true);
    channel_config_set_write_increment(&c_tx, false);
    
    dma_channel_configure(m_dma_tx, &c_tx,
        &spi_get_hw(SPI_PORT)->dr,
        TX_BUFFER,
        11,
        false
    );

    // RX Channel Config
    dma_channel_config c_rx = dma_channel_get_default_config(m_dma_rx);
    channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_8);
    channel_config_set_dreq(&c_rx, spi_get_dreq(SPI_PORT, false));
    channel_config_set_read_increment(&c_rx, false);
    channel_config_set_write_increment(&c_rx, true);
    
    dma_channel_configure(m_dma_rx, &c_rx,
        nullptr,
        &spi_get_hw(SPI_PORT)->dr,
        11,
        false
    );

    // Enable interrupt on RX complete
    dma_channel_set_irq0_enabled(m_dma_rx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, max2253x_dma_isr_wrapper);
    irq_set_enabled(DMA_IRQ_0, true);
}

void MAX2253x_MultiADC::start_async_read() {
    if (m_async_busy || m_devices.empty()) return;
    
    m_async_busy = true;
    m_async_ready = false;
    m_async_current_dev = 0;

    m_devices[0].begin_transaction();

    dma_channel_set_read_addr(m_dma_tx, TX_BUFFER, false);
    dma_channel_set_write_addr(m_dma_rx, m_async_rx_buffers[0].data(), false);

    dma_start_channel_mask((1u << m_dma_tx) | (1u << m_dma_rx));
}

void MAX2253x_MultiADC::dma_isr() {
    // Clear interrupt
    dma_hw->ints0 = 1u << m_dma_rx;

    // Finish current
    m_devices[m_async_current_dev].end_transaction();
    m_async_current_dev++;

    if (m_async_current_dev < m_devices.size()) {
        m_devices[m_async_current_dev].begin_transaction();
        
        dma_channel_set_read_addr(m_dma_tx, TX_BUFFER, false);
        dma_channel_set_write_addr(m_dma_rx, m_async_rx_buffers[m_async_current_dev].data(), false);
        
        dma_start_channel_mask((1u << m_dma_tx) | (1u << m_dma_rx));
    } else {
        m_async_busy = false;
        m_async_ready = true;
    }
}

void MAX2253x_MultiADC::process_async_data() {
    if (!m_async_ready) return;
    m_async_ready = false;
    
    const float scale = VOLTAGE_REFERENCE / static_cast<float>(ADC_MAX_VALUE);
    
    for (size_t i = 0; i < m_devices.size(); i++) {
        const auto& rx = m_async_rx_buffers[i];
        
        m_raw_cache[i][0] = ((rx[1] << 8) | rx[2]) & 0x0FFF;
        m_raw_cache[i][1] = ((rx[3] << 8) | rx[4]) & 0x0FFF;
        m_raw_cache[i][2] = ((rx[5] << 8) | rx[6]) & 0x0FFF;
        m_raw_cache[i][3] = ((rx[7] << 8) | rx[8]) & 0x0FFF;
        
        m_voltage_cache[i][0] = m_raw_cache[i][0] * scale;
        m_voltage_cache[i][1] = m_raw_cache[i][1] * scale;
        m_voltage_cache[i][2] = m_raw_cache[i][2] * scale;
        m_voltage_cache[i][3] = m_raw_cache[i][3] * scale;
    }
}