#ifndef SERIAL_PROCESSOR_H
#define SERIAL_PROCESSOR_H

#include "CommandManager.h"
#include "pico/stdlib.h"

class SerialProcessor {
    static constexpr size_t LINE_SIZE = 64;
    char line_[LINE_SIZE];
    uint8_t idx_;
    
public:
    SerialProcessor() : idx_(0) {}
    
    void poll() {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return;
        
        if (c == '\r' || c == '\n') {
            if (idx_ > 0) {
                line_[idx_] = '\0';
                CommandManager::instance().processLine(line_);
                idx_ = 0;
            }
        } else if (idx_ < LINE_SIZE - 1) {
            line_[idx_++] = static_cast<char>(c);
        }
    }
};

#endif