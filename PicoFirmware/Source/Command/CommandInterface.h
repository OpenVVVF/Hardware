#ifndef COMMAND_INTERFACE_H
#define COMMAND_INTERFACE_H

#include "CommandTypes.h"
#include "CommandContext.h"
#include <initializer_list>
#include <Telemetry.h>

class CommandInterface {
public:
    static constexpr int MAX_ARGS = 8;


    CommandInterface(const char* name, const char* desc)
        : name_(name), desc_(desc), count_(0) {}

    CommandInterface(const char* name, const char* desc, const ArgSpec& one)
        : name_(name), desc_(desc), count_(1) {
        specs_[0] = one;
    }

    CommandInterface(const char* name, const char* desc, std::initializer_list<ArgSpec> many)
        : name_(name), desc_(desc) {
        int n = (int)many.size();
        if (n > MAX_ARGS) n = MAX_ARGS;

        count_ = n;
        int i = 0;
        for (const auto& s : many) {
            if (i >= count_) break;
            specs_[i++] = s;
        }
    }

    virtual ~CommandInterface() = default;

    virtual const char* getCommandName() const { return name_; }
    virtual const char* getShortDescription() const { return desc_; }

    virtual int getArgCount() const { return count_; }

    virtual ArgSpec getArgSpec(int index) const {
        return (index >= 0 && index < count_) ? specs_[index] : ArgSpec{};
    }

    virtual void execute(const ArgValue* args, CommandContext& ctx) = 0;

private:
    const char* name_;
    const char* desc_;
    ArgSpec specs_[MAX_ARGS]{};
    int count_{0};
};

#endif
