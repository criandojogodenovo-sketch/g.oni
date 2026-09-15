#include "eng/serial/Json.hpp"

#include <utility>

namespace eng::serial {

namespace {

using eng::core::Error;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

/// Profundidade máxima real (varredura iterativa — sem recursão).
[[nodiscard]] std::size_t depthOf(const nlohmann::json& value)
{
    struct Frame {
        const nlohmann::json* value;
        std::size_t depth;
    };
    std::vector<Frame> stack;
    stack.push_back({&value, 1});
    std::size_t maxDepth = 0;
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        maxDepth = frame.depth > maxDepth ? frame.depth : maxDepth;
        if (frame.value->is_array() || frame.value->is_object()) {
            for (auto it = frame.value->begin(); it != frame.value->end();
                 ++it) {
                stack.push_back({&(*it), frame.depth + 1});
            }
        }
    }
    return maxDepth;
}

} // namespace

// =============================================================================
// JsonValue — fábricas e consultas
// =============================================================================

JsonValue JsonValue::null()
{
    return JsonValue(nlohmann::json::value_t::null);
}

JsonValue JsonValue::boolean(bool value)
{
    return JsonValue(nlohmann::json(value));
}

JsonValue JsonValue::integer(std::int64_t value)
{
    return JsonValue(nlohmann::json(value));
}

JsonValue JsonValue::uinteger(std::uint64_t value)
{
    return JsonValue(nlohmann::json(value));
}

JsonValue JsonValue::real(double value)
{
    return JsonValue(nlohmann::json(value));
}

JsonValue JsonValue::string(std::string_view value)
{
    return JsonValue(nlohmann::json(std::string(value)));
}

JsonValue JsonValue::array()
{
    return JsonValue(nlohmann::json::array());
}

JsonValue JsonValue::object()
{
    return JsonValue(nlohmann::json::object());
}

bool JsonValue::isNull() const { return value_.is_null(); }
bool JsonValue::isBool() const { return value_.is_boolean(); }
/// Atenção à semântica NÃO-nlohmann (deliberada): isInteger() é
/// ESTRITAMENTE com sinal; unsigned tem o próprio isUnsigned(). "qualquer
/// inteiro" = isInteger() || isUnsigned().
bool JsonValue::isInteger() const
{
    return value_.is_number_integer() && !value_.is_number_unsigned();
}
bool JsonValue::isUnsigned() const { return value_.is_number_unsigned(); }
bool JsonValue::isNumber() const { return value_.is_number(); }
bool JsonValue::isString() const { return value_.is_string(); }
bool JsonValue::isArray() const { return value_.is_array(); }
bool JsonValue::isObject() const { return value_.is_object(); }

std::size_t JsonValue::size() const { return value_.size(); }

bool JsonValue::asBool() const { return value_.get<bool>(); }

std::int64_t JsonValue::asI64() const
{
    return value_.get<std::int64_t>();
}

std::uint64_t JsonValue::asU64() const
{
    return value_.get<std::uint64_t>();
}

double JsonValue::asF64() const { return value_.get<double>(); }

const std::string& JsonValue::asString() const
{
    return value_.get_ref<const std::string&>();
}

JsonValue JsonValue::at(std::size_t index) const
{
    if (!isArray() || index >= size()) {
        return null(); // contrato: pré-checado; fora → null seguro (sem UB)
    }
    return JsonValue(value_[index]);
}

std::optional<JsonValue> JsonValue::find(std::string_view key) const
{
    if (!isObject()) {
        return std::nullopt;
    }
    const auto it = value_.find(std::string(key));
    if (it == value_.end()) {
        return std::nullopt;
    }
    return JsonValue(*it);
}

void JsonValue::eachKey(
    const std::function<void(std::string_view)>& fn) const
{
    for (auto it = value_.begin(); it != value_.end(); ++it) {
        fn(it.key());
    }
}

void JsonValue::append(JsonValue value)
{
    value_.push_back(std::move(value.raw()));
}

void JsonValue::set(std::string_view key, JsonValue value)
{
    value_[std::string(key)] = std::move(value.raw());
}

std::string JsonValue::dump() const
{
    // replace: UTF-8 inválido vira U+FFFD — determinístico, sem abort
    // (entrada válida → saída idêntica; ADR-030).
    return value_.dump(-1, ' ', false,
                       nlohmann::json::error_handler_t::replace);
}

// =============================================================================
// parse/dump
// =============================================================================

eng::core::Result<JsonValue> parseJson(std::string_view text,
                                       const JsonLimits& limits)
{
    if (text.size() > limits.maxTextBytes) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "parseJson: " + std::to_string(text.size()) + " bytes excede o "
            "máximo de " + std::to_string(limits.maxTextBytes)});
    }

    // allow_exceptions=false: erro de sintaxe → valor discarded (sem throw).
    nlohmann::json parsed = nlohmann::json::parse(
        text.begin(), text.end(), /*callback=*/nullptr,
        /*allow_exceptions=*/false);

    if (parsed.is_discarded()) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "parseJson: sintaxe JSON inválida (RFC 8259)"});
    }

    const std::size_t depth = depthOf(parsed);
    if (depth > limits.maxDepth) {
        return makeUnexpected(Error{
            StatusCode::ParseError,
            "parseJson: profundidade " + std::to_string(depth) +
                " excede o máximo de " + std::to_string(limits.maxDepth)});
    }

    return JsonValue(std::move(parsed));
}

std::string dumpJson(const JsonValue& value)
{
    return value.dump();
}

} // namespace eng::serial
