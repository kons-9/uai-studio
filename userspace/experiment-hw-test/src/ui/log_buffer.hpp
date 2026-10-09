#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace experiment::hwtest {

class LogBuffer {
public:
    static constexpr std::size_t kCapacity = 128;
    static constexpr std::size_t kColumns = 62;
    static constexpr std::size_t kVisible = 19;
    struct Line {
        char text[kColumns + 1]{};
        bool failed = false;
        bool passed = false;
    };

    void Write(const char *text)
    {
        if (!text) {
            return;
        }
        char name[49]{}, outcome[8]{};
        if (std::sscanf(text, "HWTEST %48s %7s", name, outcome) == 2) {
            if (std::strcmp(outcome, "START") == 0) {
                std::snprintf(status_, sizeof(status_), "%s", name);
            } else if (std::strcmp(outcome, "PASS") == 0) {
                ++passed_;
            } else if (std::strcmp(outcome, "FAIL") == 0) {
                ++failed_;
            }
        }
        unsigned passed = 0, failed = 0;
        if (std::sscanf(text, "HWTEST SUMMARY pass=%u fail=%u", &passed, &failed) == 2) {
            passed_ = passed;
            failed_ = failed;
            std::snprintf(status_, sizeof(status_), "COMPLETE");
        }
        const bool is_fail = std::strstr(text, " FAIL") != nullptr;
        const bool is_pass = std::strstr(text, " PASS") != nullptr;
        for (; *text; ++text) {
            if (*text == '\r') {
                continue;
            }
            if (*text == '\n') {
                Advance();
                continue;
            }
            if (column_ == kColumns) {
                Advance();
            }
            auto &line = lines_[head_];
            line.text[column_++] = *text >= ' ' && *text <= '~' ? *text : '?';
            line.text[column_] = '\0';
            line.failed = is_fail;
            line.passed = is_pass;
        }
    }

    void Begin()
    {
        passed_ = failed_ = 0;
        offset_ = 0;
        std::snprintf(status_, sizeof(status_), "RUNNING");
    }

    void Scroll(bool older)
    {
        const auto limit = count_ > kVisible ? count_ - kVisible : 0;
        if (older && offset_ < limit) {
            ++offset_;
        } else if (!older && offset_ > 0) {
            --offset_;
        }
    }

    Line Visible(std::size_t row) const
    {
        const auto available = count_ - offset_;
        const auto visible = available < kVisible ? available : kVisible;
        if (row >= visible) {
            return {};
        }
        const auto end = (head_ + kCapacity - offset_) % kCapacity;
        return lines_[(end + kCapacity + 1 - visible + row) % kCapacity];
    }

    unsigned Passed() const { return passed_; }
    unsigned Failed() const { return failed_; }
    const char *Status() const { return status_; }

private:
    void Advance()
    {
        head_ = (head_ + 1) % kCapacity;
        lines_[head_] = {};
        column_ = 0;
        if (count_ < kCapacity) {
            ++count_;
        }
        if (offset_ > 0 && offset_ < count_ - 1) {
            ++offset_;
        }
    }

    Line lines_[kCapacity]{};
    std::size_t head_ = 0, column_ = 0, count_ = 1, offset_ = 0;
    unsigned passed_ = 0, failed_ = 0;
    char status_[49] = "STARTING";
};

}