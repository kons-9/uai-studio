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
                progress_name_[0] = '\0';
                progress_current_ = progress_total_ = 0;
            } else if (std::strcmp(outcome, "PASS") == 0) {
                ++passed_;
                if (!expected_total_set_) { ++total_; }
            } else if (std::strcmp(outcome, "FAIL") == 0) {
                ++failed_;
                if (!expected_total_set_) { ++total_; }
            }
        }
        unsigned passed = 0, failed = 0, total = 0;
        if (std::sscanf(text, "HWTEST SUMMARY pass=%u fail=%u total=%u", &passed, &failed, &total) == 3) {
            passed_ = passed;
            failed_ = failed;
            total_ = total;
            expected_total_set_ = true;
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

    void Begin(unsigned expected_total = 0)
    {
        passed_ = failed_ = 0;
        total_ = expected_total;
        expected_total_set_ = expected_total != 0;
        for (auto &line : lines_) {
            line = {};
        }
        head_ = column_ = offset_ = 0;
        count_ = 1;
        progress_name_[0] = '\0';
        progress_current_ = progress_total_ = 0;
        std::snprintf(status_, sizeof(status_), "RUNNING");
    }

    void Progress(const char *name, unsigned current, unsigned total)
    {
        if (!name || total == 0) {
            return;
        }
        std::snprintf(progress_name_, sizeof(progress_name_), "%s", name);
        progress_current_ = current;
        progress_total_ = total;
        std::snprintf(status_, sizeof(status_), "%s %u/%u", name, current, total);
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
    unsigned Total() const { return total_; }
    const char *Status() const { return status_; }
    const char *ProgressName() const { return progress_name_; }
    unsigned ProgressCurrent() const { return progress_current_; }
    unsigned ProgressTotal() const { return progress_total_; }

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
    unsigned passed_ = 0, failed_ = 0, total_ = 0;
    bool expected_total_set_ = false;
    char status_[49] = "STARTING";
    char progress_name_[49]{};
    unsigned progress_current_ = 0, progress_total_ = 0;
};

}
