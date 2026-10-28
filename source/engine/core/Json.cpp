#include "core/Json.hpp"

#include <tinygltf_json_c.h>

#include <utility>

namespace mengine {

bool JsonValue::IsObject() const
{
    return m_value != nullptr && m_value->type == TG3JSON_OBJECT;
}

bool JsonValue::IsArray() const
{
    return m_value != nullptr && m_value->type == TG3JSON_ARRAY;
}

bool JsonValue::IsNumber() const
{
    return m_value != nullptr && (m_value->type == TG3JSON_INT || m_value->type == TG3JSON_REAL);
}

bool JsonValue::IsString() const
{
    return m_value != nullptr && m_value->type == TG3JSON_STRING;
}

JsonValue JsonValue::operator[](std::string_view key) const
{
    if (!IsObject())
    {
        return JsonValue();
    }
    return JsonValue(tg3json_object_get_n(m_value, key.data(), key.size()));
}

JsonValue JsonValue::At(size_t index) const
{
    if (!IsArray())
    {
        return JsonValue();
    }
    return JsonValue(tg3json_array_get(m_value, index));
}

size_t JsonValue::Size() const
{
    if (IsArray())
    {
        return tg3json_array_size(m_value);
    }
    if (IsObject())
    {
        return tg3json_object_size(m_value);
    }
    return 0;
}

std::optional<double> JsonValue::AsNumber() const
{
    if (m_value == nullptr)
    {
        return std::nullopt;
    }
    // "1" parses as an integer and "1.0" as a real; both are numbers here.
    if (m_value->type == TG3JSON_INT)
    {
        return static_cast<double>(m_value->u.integer);
    }
    if (m_value->type == TG3JSON_REAL)
    {
        return m_value->u.real;
    }
    return std::nullopt;
}

std::optional<std::string_view> JsonValue::AsString() const
{
    if (!IsString())
    {
        return std::nullopt;
    }
    return std::string_view(m_value->u.string.ptr, m_value->u.string.len);
}

std::optional<bool> JsonValue::AsBool() const
{
    if (m_value == nullptr || m_value->type != TG3JSON_BOOL)
    {
        return std::nullopt;
    }
    return m_value->u.boolean != 0;
}

JsonDocument::JsonDocument() = default;

JsonDocument::~JsonDocument()
{
    if (m_root != nullptr)
    {
        tg3json_value_free(m_root.get());
    }
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept : m_root(std::move(other.m_root)) {}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept
{
    if (this != &other)
    {
        if (m_root != nullptr)
        {
            tg3json_value_free(m_root.get());
        }
        m_root = std::move(other.m_root);
    }
    return *this;
}

std::optional<JsonDocument> JsonDocument::Parse(std::string_view text, std::string* error)
{
    auto root = std::make_unique<tg3json_value>();
    const char* errorPosition = nullptr;
    if (tg3json_parse_n(text.data(), text.size(), 0, root.get(), &errorPosition) == 0)
    {
        if (error != nullptr)
        {
            const size_t offset = errorPosition != nullptr && errorPosition >= text.data()
                ? static_cast<size_t>(errorPosition - text.data())
                : 0;
            *error = "JSON syntax error at byte " + std::to_string(offset);
        }
        return std::nullopt;
    }

    JsonDocument document;
    document.m_root = std::move(root);
    return document;
}

JsonValue JsonDocument::Root() const
{
    return JsonValue(m_root.get());
}

} // namespace mengine
