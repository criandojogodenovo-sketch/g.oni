/// Implementações do modelo de valores (NiValue.hpp — FASE 11).

#include <string>
#include <string_view>

#include "eng/niscript/NiValue.hpp"

namespace eng::ni {

std::string_view niTypeName(NiType type) noexcept
{
    switch (type) {
    case NiType::Nil: return "nil";
    case NiType::Int: return "int";
    case NiType::Float: return "float";
    case NiType::Bool: return "bool";
    case NiType::String: return "string";
    case NiType::Vec2: return "vec2";
    case NiType::Vec3: return "vec3";
    case NiType::Color: return "color";
    case NiType::Entity: return "entity";
    case NiType::Asset: return "asset";
    case NiType::Transform: return "transform";
    case NiType::CompView: return "componente";
    case NiType::Dynamic: return "dinâmico";
    }
    return "?";
}

NiValue niInt(std::int64_t v) noexcept
{
    NiValue value;
    value.type = NiType::Int;
    value.i = v;
    return value;
}

NiValue niFloat(double v) noexcept
{
    NiValue value;
    value.type = NiType::Float;
    value.d[0] = v;
    return value;
}

NiValue niBool(bool v) noexcept
{
    NiValue value;
    value.type = NiType::Bool;
    value.i = v ? 1 : 0;
    return value;
}

NiValue niString(std::string v) noexcept
{
    NiValue value;
    value.type = NiType::String;
    value.s = std::move(v);
    return value;
}

NiValue niVec2(double x, double y) noexcept
{
    NiValue value;
    value.type = NiType::Vec2;
    value.d[0] = x;
    value.d[1] = y;
    return value;
}

NiValue niVec3(double x, double y, double z) noexcept
{
    NiValue value;
    value.type = NiType::Vec3;
    value.d[0] = x;
    value.d[1] = y;
    value.d[2] = z;
    return value;
}

NiValue niColor(double r, double g, double b, double a) noexcept
{
    NiValue value;
    value.type = NiType::Color;
    value.d[0] = r;
    value.d[1] = g;
    value.d[2] = b;
    value.d[3] = a;
    return value;
}

NiValue niTransform(const NiTransform& t) noexcept
{
    NiValue value;
    value.type = NiType::Transform;
    value.d[0] = t.position.x;
    value.d[1] = t.position.y;
    value.d[2] = t.position.z;
    value.d[3] = t.rotationDegrees.x;
    value.d[4] = t.rotationDegrees.y;
    value.d[5] = t.rotationDegrees.z;
    value.d[6] = t.scale.x;
    value.d[7] = t.scale.y;
    value.d[8] = t.scale.z;
    return value;
}

NiValue niAsset(std::uint64_t id) noexcept
{
    NiValue value;
    value.type = NiType::Asset;
    value.i = static_cast<std::int64_t>(id);
    return value;
}

NiValue niCompView(eng::ecs::Entity e, std::string prefix) noexcept
{
    NiValue value;
    value.type = NiType::CompView;
    value.i = niEntityValue(e).i;
    value.s = std::move(prefix);
    return value;
}

NiValue niZero(NiType type)
{
    switch (type) {
    case NiType::Int: return niInt(0);
    case NiType::Float: return niFloat(0.0);
    case NiType::Bool: return niBool(false);
    case NiType::String: return niString("");
    case NiType::Vec2: return niVec2(0.0, 0.0);
    case NiType::Vec3: return niVec3(0.0, 0.0, 0.0);
    case NiType::Color: return niColor(0.0, 0.0, 0.0, 1.0);
    case NiType::Transform: {
        NiTransform t;
        t.scale = {1.0, 1.0, 1.0};
        return niTransform(t);
    }
    case NiType::Entity: {
        NiValue v;
        v.type = NiType::Entity;
        v.i = static_cast<std::int64_t>(kNoEntityPacked);
        return v;
    }
    case NiType::Asset: return niAsset(0);
    case NiType::Nil: case NiType::CompView: case NiType::Dynamic: break;
    }
    return NiValue{}; // nil
}

std::string_view NiFault::kindName(Kind kind) noexcept
{
    switch (kind) {
    case Kind::Type: return "Type";
    case Kind::NilUse: return "NilUse";
    case Kind::DivByZero: return "DivByZero";
    case Kind::Range: return "Range";
    case Kind::Timeout: return "Timeout";
    case Kind::RepeatLimit: return "RepeatLimit";
    case Kind::EmitDepth: return "EmitDepth";
    case Kind::Stack: return "Stack";
    case Kind::EntityStale: return "EntityStale";
    case Kind::EntityNull: return "EntityNull";
    case Kind::ComponentMissing: return "ComponentMissing";
    case Kind::FieldUnknown: return "FieldUnknown";
    case Kind::BadArgument: return "BadArgument";
    case Kind::LinkLimit: return "LinkLimit";
    case Kind::NativeError: return "NativeError";
    case Kind::BadWrite: return "BadWrite";
    }
    return "?";
}

} // namespace eng::ni
