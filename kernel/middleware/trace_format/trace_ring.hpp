#pragma once

#include <cstddef>
#include <cstdint>

namespace uai::ai::middleware::trace_format {

template <typename Header, typename Record, typename Flush,
          typename UpdateHeader>
bool AppendTraceRecord(Header *header, Record *records,
                       std::uint32_t capacity, const Record &source_record,
                       std::uint32_t commit_magic, Flush flush,
                       UpdateHeader update_header)
{
    if (header == nullptr || records == nullptr || capacity == 0U ||
        header->capacity != capacity || header->write_index >= capacity) {
        return false;
    }

    Record record = source_record;
    const std::uint32_t sequence = header->next_sequence;
    record.sequence = sequence;
    record.commit_marker = commit_magic ^ sequence;
    Record &slot = records[header->write_index];
    slot = record;
    flush(&slot, sizeof(slot));
    if (header->record_count < capacity) {
        ++header->record_count;
    } else {
        ++header->dropped_count;
    }
    header->write_index = (header->write_index + 1U) % capacity;
    header->next_sequence = sequence + 1U;
    update_header(*header, record);
    flush(header, sizeof(*header));
    return true;
}

} // namespace uai::ai::middleware::trace_format
