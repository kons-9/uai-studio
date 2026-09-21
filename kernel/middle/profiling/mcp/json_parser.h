/*
 * json_parser.h — Lightweight JSON parser for MCU (no heap allocation)
 *
 * Operates on a fixed input buffer. Extracts values by key path.
 * Supports: strings, numbers, booleans, null, nested objects, arrays.
 * Does NOT build a DOM tree — streaming extraction only.
 *
 * RAM budget: ~200 bytes stack + input buffer.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace uai {

/* ------------------------------------------------------------------ */
/*  JSON value types                                                   */
/* ------------------------------------------------------------------ */

enum class JsonType : uint8_t {
    NONE,
    STRING,
    NUMBER,
    BOOL,
    NULL_VAL,
    OBJECT,
    ARRAY,
};

/* ------------------------------------------------------------------ */
/*  JSON value (non-owning view into source buffer)                   */
/* ------------------------------------------------------------------ */

struct JsonValue {
    JsonType    type   = JsonType::NONE;
    const char *start  = nullptr;  /* pointer into source buffer */
    size_t      length = 0;

    bool is_valid() const { return type != JsonType::NONE; }

    /* Extract string (copies into dst, NUL-terminates) */
    bool as_string(char *dst, size_t dst_len) const
    {
        if (type != JsonType::STRING || !dst || dst_len == 0)
            return false;
        size_t copy_len = (length < dst_len - 1) ? length : (dst_len - 1);
        std::memcpy(dst, start, copy_len);
        dst[copy_len] = '\0';
        return true;
    }

    /* Extract integer */
    bool as_int(int32_t &out) const
    {
        if (type != JsonType::NUMBER) return false;
        out = 0;
        bool neg = false;
        size_t i = 0;
        if (length > 0 && start[0] == '-') { neg = true; i = 1; }
        for (; i < length; i++) {
            char c = start[i];
            if (c < '0' || c > '9') break;
            out = out * 10 + (c - '0');
        }
        if (neg) out = -out;
        return true;
    }

    /* Extract unsigned integer */
    bool as_uint(uint32_t &out) const
    {
        if (type != JsonType::NUMBER) return false;
        out = 0;
        for (size_t i = 0; i < length; i++) {
            char c = start[i];
            if (c < '0' || c > '9') break;
            out = out * 10 + static_cast<uint32_t>(c - '0');
        }
        return true;
    }

    /* Extract boolean */
    bool as_bool(bool &out) const
    {
        if (type != JsonType::BOOL) return false;
        out = (length >= 4 && start[0] == 't');
        return true;
    }
};

/* ------------------------------------------------------------------ */
/*  JSON parser                                                        */
/* ------------------------------------------------------------------ */

class JsonParser {
public:
    JsonParser(const char *json, size_t length)
        : src_(json), len_(length), pos_(0) {}

    /* Find a value by key in the current object level */
    JsonValue find(const char *key)
    {
        pos_ = 0;
        return find_key(key);
    }

    /* Find nested key: e.g. find_nested("params", "name") */
    JsonValue find_nested(const char *key1, const char *key2)
    {
        pos_ = 0;
        JsonValue obj = find_key(key1);
        if (obj.type != JsonType::OBJECT) return {};

        /* Parse inside the object */
        size_t saved = pos_;
        pos_ = static_cast<size_t>(obj.start - src_);
        JsonValue result = find_key(key2);
        if (!result.is_valid()) pos_ = saved;
        return result;
    }

    /* Find 3-level nested key: e.g. find_nested3("params", "arguments", "name") */
    JsonValue find_nested3(const char *key1, const char *key2, const char *key3)
    {
        pos_ = 0;
        JsonValue obj1 = find_key(key1);
        if (obj1.type != JsonType::OBJECT) return {};

        pos_ = static_cast<size_t>(obj1.start - src_);
        JsonValue obj2 = find_key(key2);
        if (obj2.type != JsonType::OBJECT) return {};

        pos_ = static_cast<size_t>(obj2.start - src_);
        return find_key(key3);
    }

    /* Extract the method name from a JSON-RPC message */
    bool get_method(char *out, size_t out_len)
    {
        JsonValue v = find("method");
        return v.as_string(out, out_len);
    }

    /* Extract the id from a JSON-RPC message */
    bool get_id(int32_t &out)
    {
        JsonValue v = find("id");
        return v.as_int(out);
    }

private:
    void skip_whitespace()
    {
        while (pos_ < len_ && (src_[pos_] == ' ' || src_[pos_] == '\n'
               || src_[pos_] == '\r' || src_[pos_] == '\t'))
            pos_++;
    }

    bool match(char c)
    {
        skip_whitespace();
        if (pos_ < len_ && src_[pos_] == c) { pos_++; return true; }
        return false;
    }

    /* Skip a JSON value (string, number, object, array, bool, null) */
    void skip_value()
    {
        skip_whitespace();
        if (pos_ >= len_) return;

        char c = src_[pos_];
        if (c == '"') {
            skip_string();
        } else if (c == '{') {
            skip_object();
        } else if (c == '[') {
            skip_array();
        } else if (c == 't' || c == 'f') {
            while (pos_ < len_ && src_[pos_] >= 'a' && src_[pos_] <= 'z') pos_++;
        } else if (c == 'n') {
            while (pos_ < len_ && src_[pos_] >= 'a' && src_[pos_] <= 'z') pos_++;
        } else {
            /* number */
            while (pos_ < len_ && ((src_[pos_] >= '0' && src_[pos_] <= '9')
                   || src_[pos_] == '-' || src_[pos_] == '.'
                   || src_[pos_] == 'e' || src_[pos_] == 'E'
                   || src_[pos_] == '+'))
                pos_++;
        }
    }

    void skip_string()
    {
        if (pos_ < len_ && src_[pos_] == '"') pos_++;
        while (pos_ < len_) {
            if (src_[pos_] == '\\') { pos_ += 2; continue; }
            if (src_[pos_] == '"') { pos_++; return; }
            pos_++;
        }
    }

    void skip_object()
    {
        if (!match('{')) return;
        int depth = 1;
        while (pos_ < len_ && depth > 0) {
            if (src_[pos_] == '{') depth++;
            else if (src_[pos_] == '}') depth--;
            else if (src_[pos_] == '"') { skip_string(); continue; }
            pos_++;
        }
    }

    void skip_array()
    {
        if (!match('[')) return;
        int depth = 1;
        while (pos_ < len_ && depth > 0) {
            if (src_[pos_] == '[') depth++;
            else if (src_[pos_] == ']') depth--;
            else if (src_[pos_] == '"') { skip_string(); continue; }
            pos_++;
        }
    }

    /* Read a quoted string, return pointer and length (without quotes) */
    JsonValue read_string()
    {
        skip_whitespace();
        if (pos_ >= len_ || src_[pos_] != '"') return {};
        pos_++; /* skip opening quote */
        const char *start = &src_[pos_];
        size_t slen = 0;
        while (pos_ < len_) {
            if (src_[pos_] == '\\') { pos_ += 2; slen += 2; continue; }
            if (src_[pos_] == '"') { pos_++; break; }
            pos_++; slen++;
        }
        return { JsonType::STRING, start, slen };
    }

    /* Read any JSON value */
    JsonValue read_value()
    {
        skip_whitespace();
        if (pos_ >= len_) return {};

        char c = src_[pos_];

        if (c == '"') {
            return read_string();
        }
        if (c == '{') {
            const char *start = &src_[pos_];
            size_t ostart = pos_;
            skip_object();
            return { JsonType::OBJECT, start, pos_ - ostart };
        }
        if (c == '[') {
            const char *start = &src_[pos_];
            size_t astart = pos_;
            skip_array();
            return { JsonType::ARRAY, start, pos_ - astart };
        }
        if (c == 't' || c == 'f') {
            const char *start = &src_[pos_];
            size_t bstart = pos_;
            while (pos_ < len_ && src_[pos_] >= 'a' && src_[pos_] <= 'z') pos_++;
            return { JsonType::BOOL, start, pos_ - bstart };
        }
        if (c == 'n') {
            const char *start = &src_[pos_];
            size_t nstart = pos_;
            while (pos_ < len_ && src_[pos_] >= 'a' && src_[pos_] <= 'z') pos_++;
            return { JsonType::NULL_VAL, start, pos_ - nstart };
        }
        /* number */
        {
            const char *start = &src_[pos_];
            size_t numstart = pos_;
            while (pos_ < len_ && ((src_[pos_] >= '0' && src_[pos_] <= '9')
                   || src_[pos_] == '-' || src_[pos_] == '.'
                   || src_[pos_] == 'e' || src_[pos_] == 'E'
                   || src_[pos_] == '+'))
                pos_++;
            return { JsonType::NUMBER, start, pos_ - numstart };
        }
    }

    /* Find key at current object level */
    JsonValue find_key(const char *key)
    {
        size_t key_len = std::strlen(key);

        /* Scan for opening '{' */
        skip_whitespace();
        while (pos_ < len_ && src_[pos_] != '{') pos_++;
        if (pos_ >= len_) return {};
        pos_++; /* skip '{' */

        while (pos_ < len_) {
            skip_whitespace();
            if (pos_ < len_ && src_[pos_] == '}') return {}; /* end of object */

            /* Read key string */
            JsonValue k = read_string();
            if (!k.is_valid()) return {};

            /* Skip ':' */
            if (!match(':')) return {};

            /* Check if this is the key we want */
            if (k.length == key_len &&
                std::memcmp(k.start, key, key_len) == 0) {
                return read_value();
            }

            /* Not our key — skip value */
            skip_value();

            /* Skip comma */
            skip_whitespace();
            if (pos_ < len_ && src_[pos_] == ',') pos_++;
        }

        return {};
    }

    const char *src_;
    size_t      len_;
    size_t      pos_;
};

/* ------------------------------------------------------------------ */
/*  JSON writer (for building response messages)                      */
/* ------------------------------------------------------------------ */

class JsonWriter {
public:
    JsonWriter(char *buf, size_t buf_len)
        : buf_(buf), cap_(buf_len), pos_(0)
    {
        if (cap_ > 0) buf_[0] = '\0';
    }

    void begin_object()     { write_char('{'); first_ = true; }
    void end_object()       { write_char('}'); }
    void begin_array()      { write_char('['); first_ = true; }
    void end_array()        { write_char(']'); }

    void key(const char *k)
    {
        if (!first_) write_char(',');
        first_ = false;
        write_char('"');
        write_str(k);
        write_char('"');
        write_char(':');
    }

    void value_string(const char *v)
    {
        write_char('"');
        write_str(v);
        write_char('"');
    }

    void value_int(int32_t v)
    {
        char tmp[12];
        int len = int_to_str(v, tmp, sizeof(tmp));
        for (int i = 0; i < len; i++) write_char(tmp[i]);
    }

    void value_uint(uint32_t v)
    {
        char tmp[11];
        int len = uint_to_str(v, tmp, sizeof(tmp));
        for (int i = 0; i < len; i++) write_char(tmp[i]);
    }

    void value_bool(bool v)
    {
        write_str(v ? "true" : "false");
    }

    void value_null()
    {
        write_str("null");
    }

    /* Write a key-value pair (string) */
    void kv_string(const char *k, const char *v)
    {
        key(k); value_string(v);
    }

    void kv_int(const char *k, int32_t v)
    {
        key(k); value_int(v);
    }

    void kv_uint(const char *k, uint32_t v)
    {
        key(k); value_uint(v);
    }

    void kv_bool(const char *k, bool v)
    {
        key(k); value_bool(v);
    }

    /* Array element helpers */
    void array_string(const char *v)
    {
        if (!first_) write_char(',');
        first_ = false;
        value_string(v);
    }

    void array_int(int32_t v)
    {
        if (!first_) write_char(',');
        first_ = false;
        value_int(v);
    }

    size_t length() const { return pos_; }
    const char *c_str() const { return buf_; }

    bool overflow() const { return overflow_; }

private:
    void write_char(char c)
    {
        if (pos_ + 1 < cap_) {
            buf_[pos_++] = c;
            buf_[pos_] = '\0';
        } else {
            overflow_ = true;
        }
    }

    void write_str(const char *s)
    {
        while (*s) write_char(*s++);
    }

    static int int_to_str(int32_t v, char *buf, size_t len)
    {
        if (len == 0) return 0;
        bool neg = v < 0;
        uint32_t uv = neg ? static_cast<uint32_t>(-v) : static_cast<uint32_t>(v);
        int n = uint_to_str(uv, buf + (neg ? 1 : 0), len - (neg ? 1 : 0));
        if (neg) { buf[0] = '-'; n++; }
        return n;
    }

    static int uint_to_str(uint32_t v, char *buf, size_t len)
    {
        char tmp[11];
        int i = 0;
        if (v == 0) { tmp[i++] = '0'; }
        else {
            while (v > 0 && i < 10) {
                tmp[i++] = '0' + static_cast<char>(v % 10);
                v /= 10;
            }
        }
        /* reverse */
        int n = (static_cast<size_t>(i) < len) ? i : static_cast<int>(len - 1);
        for (int j = 0; j < n; j++) buf[j] = tmp[n - 1 - j];
        buf[n] = '\0';
        return n;
    }

    char    *buf_;
    size_t   cap_;
    size_t   pos_;
    bool     first_    = true;
    bool     overflow_ = false;
};

} // namespace uai
