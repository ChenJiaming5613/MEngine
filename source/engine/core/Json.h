#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct tg3json_value;

namespace mengine {

// Read-only view of a value owned by a JsonDocument. Lookups on a missing or mistyped value return an
// invalid JsonValue, so chains like root["camera"]["position"].At(0).AsNumber() never crash.
class JsonValue
{
public:
    JsonValue() = default;
    explicit JsonValue(const tg3json_value* value) : m_value(value) {}

    bool IsValid() const { return m_value != nullptr; }
    bool IsObject() const;
    bool IsArray() const;
    bool IsNumber() const;
    bool IsString() const;

    JsonValue operator[](std::string_view key) const;
    JsonValue At(size_t index) const;
    size_t Size() const;

    std::optional<double> AsNumber() const;
    std::optional<std::string_view> AsString() const;
    std::optional<bool> AsBool() const;

private:
    const tg3json_value* m_value = nullptr;
};

class JsonDocument
{
public:
    JsonDocument();
    ~JsonDocument();
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;

    // Strict JSON (no comments). On failure returns nullopt and describes the error position in *error.
    static std::optional<JsonDocument> Parse(std::string_view text, std::string* error);

    JsonValue Root() const;

private:
    std::unique_ptr<tg3json_value> m_root;
};

} // namespace mengine
