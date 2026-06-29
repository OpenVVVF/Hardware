#ifndef COMMAND_TYPES_H
#define COMMAND_TYPES_H

#include <cstdint>
#include <stdio.h>

struct ArgValue {
    float f_val;
    int32_t i_val;
    bool present;
};

struct ArgSpec {
    const char* name;
    const char* unit;
    float min;
    float max;
    float default_val;
    bool required;
    enum Type { FLOAT, INT } type;
    
    void printRange(char* buf, size_t size) const {
        if (type == FLOAT) {
            snprintf(buf, size, "%.1f-%.1f %s", min, max, unit);
        } else {
            snprintf(buf, size, "%d-%d %s", (int)min, (int)max, unit);
        }
    }
};

#endif