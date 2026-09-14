#include "eng/reflect/Reflect.hpp"

#include <utility>

namespace eng::reflect {

// --- TypeRegistry: instância global + tipos embutidos ------------------------

namespace {

/// Registro dos tipos embutidos na primeira construção do global().
/// Nome canônico ↔ tipo C++ (aliasing long/int64_t documentado em ADR-021).
void registerBuiltinTypes(TypeRegistry& registry)
{
    registry.registerType(TypeKind::Primitive, "bool", sizeof(bool), alignof(bool));
    registry.registerType(TypeKind::Primitive, "i8", sizeof(char), alignof(char));
    registry.registerType(TypeKind::Primitive, "u8", sizeof(unsigned char), alignof(unsigned char));
    registry.registerType(TypeKind::Primitive, "i16", sizeof(short), alignof(short));
    registry.registerType(TypeKind::Primitive, "u16", sizeof(unsigned short), alignof(unsigned short));
    registry.registerType(TypeKind::Primitive, "i32", sizeof(int), alignof(int));
    registry.registerType(TypeKind::Primitive, "u32", sizeof(unsigned int), alignof(unsigned int));
    registry.registerType(TypeKind::Primitive, "i64", sizeof(long long), alignof(long long));
    registry.registerType(TypeKind::Primitive, "u64", sizeof(unsigned long long), alignof(unsigned long long));
    registry.registerType(TypeKind::Primitive, "f32", sizeof(float), alignof(float));
    registry.registerType(TypeKind::Primitive, "f64", sizeof(double), alignof(double));
    registry.registerType(TypeKind::Primitive, "string", sizeof(std::string), alignof(std::string));
}

} // namespace

TypeRegistry& TypeRegistry::global() noexcept
{
    static TypeRegistry instance; // magic static: construção thread-safe, única
    static const bool builtinsRegistered = [] {
        registerBuiltinTypes(instance);
        return true;
    }();
    (void)builtinsRegistered;
    return instance;
}

TypeId TypeRegistry::registerType(TypeKind kind,
                                  std::string_view name,
                                  std::size_t size,
                                  std::size_t alignment,
                                  std::span<const PropertyDesc> properties,
                                  std::span<const EnumeratorDesc> enumerators,
                                  std::string_view underlyingTypeName)
{
    const TypeId id = typeIdOf(name);

    std::unique_lock lock(mutex_);

    // Idempotência por nome: primeiro registro vence.
    if (const auto byName = byName_.find(name); byName != byName_.end()) {
        return types_[byName->second].id;
    }
    // Degradação segura em colisão de FNV (2^-64 por par — ADR-021).
    if (const auto byId = byId_.find(id); byId != byId_.end()) {
        return types_[byId->second].id;
    }

    TypeInfo info;
    info.name = name;
    info.id = id;
    info.kind = kind;
    info.size = size;
    info.alignment = alignment;
    info.properties.reserve(properties.size());
    for (const PropertyDesc& p : properties) {
        PropertyInfo copy;
        copy.name = p.name;
        copy.offset = p.offset;
        copy.typeName = p.typeName;
        // Resolução adiantada: o tipo do campo já registrado ganha typeId;
        // não registrado → 0 (resolúvel por typeName a qualquer momento).
        if (const auto field = byName_.find(p.typeName); field != byName_.end()) {
            copy.typeId = types_[field->second].id;
        }
        info.properties.push_back(std::move(copy));
    }
    info.enumerators.reserve(enumerators.size());
    for (const EnumeratorDesc& e : enumerators) {
        info.enumerators.push_back(EnumeratorInfo{std::string(e.name), e.value});
    }
    if (!underlyingTypeName.empty()) {
        if (const auto under = byName_.find(underlyingTypeName); under != byName_.end()) {
            info.underlyingTypeId = types_[under->second].id;
        } else {
            info.underlyingTypeId = typeIdOf(underlyingTypeName);
        }
    }

    const auto index = types_.size();
    types_.push_back(std::move(info)); // deque: referências anteriores estáveis
    byName_[types_[index].name] = index; // string_view para string estável no deque
    byId_[id] = index;
    return id;
}

const TypeInfo* TypeRegistry::find(std::string_view name) const
{
    std::shared_lock lock(mutex_);
    if (const auto it = byName_.find(name); it != byName_.end()) {
        return &types_[it->second];
    }
    return nullptr; // tipo não registrado: definido, sem abort
}

const TypeInfo* TypeRegistry::find(TypeId id) const
{
    std::shared_lock lock(mutex_);
    if (const auto it = byId_.find(id); it != byId_.end()) {
        return &types_[it->second];
    }
    return nullptr;
}

std::size_t TypeRegistry::count() const
{
    std::shared_lock lock(mutex_);
    return types_.size();
}

void TypeRegistry::clear()
{
    std::unique_lock lock(mutex_);
    types_.clear();
    byName_.clear();
    byId_.clear();
}

// --- detail::Registrar -------------------------------------------------------

void detail::Registrar::submit()
{
    (void)TypeRegistry::global().registerType(
        kind_, name_, size_, alignment_, properties_, enumerators_, underlying_);
}

} // namespace eng::reflect
