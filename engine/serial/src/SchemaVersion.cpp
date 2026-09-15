#include "eng/serial/SchemaVersion.hpp"

#include <algorithm>

namespace eng::serial {

namespace {

using eng::core::Error;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

} // namespace

eng::core::Result<void> MigrationRegistry::add(std::unique_ptr<Migration> step)
{
    if (step == nullptr) {
        return makeUnexpected(Error{StatusCode::InvalidArgument,
                                    "MigrationRegistry::add: passo nulo"});
    }
    if (step->from() >= step->to()) {
        return makeUnexpected(
            Error{StatusCode::InvalidArgument,
                  "MigrationRegistry::add: from() " +
                      std::to_string(step->from()) + " >= to() " +
                      std::to_string(step->to())});
    }
    for (const auto& existing : steps_) {
        if (existing->from() == step->from()) {
            return makeUnexpected(
                Error{StatusCode::AlreadyExists,
                      "MigrationRegistry::add: já existe passo saindo de " +
                          std::to_string(step->from())});
        }
    }
    steps_.push_back(std::move(step));
    std::sort(steps_.begin(), steps_.end(),
              [](const std::unique_ptr<Migration>& a,
                 const std::unique_ptr<Migration>& b) {
                  return a->from() < b->from();
              });
    return {};
}

eng::core::Result<JsonValue> MigrationRegistry::migrateTo(
    JsonValue data, SchemaVersion current, SchemaVersion target) const
{
    if (current > target) {
        return makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "migrateTo: downgrade (de " + std::to_string(current) + " para " +
                std::to_string(target) + ") não existe"});
    }

    SchemaVersion cursor = current;
    while (cursor < target) {
        const auto it = std::find_if(
            steps_.begin(), steps_.end(),
            [&](const std::unique_ptr<Migration>& step) {
                return step->from() == cursor;
            });
        if (it == steps_.end()) {
            return makeUnexpected(Error{
                StatusCode::NotSupported,
                "migrateTo: sem migration registrada a partir da versão " +
                    std::to_string(cursor) + " (alvo " +
                    std::to_string(target) + ")"});
        }
        if ((*it)->to() > target) {
            // Passo que ultrapassa o alvo: schema do leitor é mais antigo
            // que o arquivo — erro claro, não "melhor esforço".
            return makeUnexpected(Error{
                StatusCode::NotSupported,
                "migrateTo: o passo " + std::to_string((*it)->from()) +
                    "→" + std::to_string((*it)->to()) +
                    " ultrapassa o alvo " + std::to_string(target)});
        }
        auto migrated = (*it)->migrate(std::move(data));
        if (migrated.isError()) {
            return makeUnexpected(
                Error{StatusCode::ParseError,
                      "migrateTo: falha no passo " +
                          std::to_string((*it)->from()) + "→" +
                          std::to_string((*it)->to()) + ": " +
                          migrated.error().message});
        }
        cursor = (*it)->to();
        data = std::move(migrated.value());
    }
    return data;
}

SchemaVersion MigrationRegistry::latestKnown() const noexcept
{
    SchemaVersion latest = 0;
    for (const auto& step : steps_) {
        latest = step->to() > latest ? step->to() : latest;
    }
    return latest;
}

} // namespace eng::serial
