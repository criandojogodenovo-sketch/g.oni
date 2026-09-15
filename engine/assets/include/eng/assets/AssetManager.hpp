#pragma once

/// eng::assets::AssetManager + loaders — cache runtime de assets
/// (FASE 3; ADR-029).
///
/// - SINGLE-THREADED nesta fase (missão §2.9; ADR-034). A interface é
///   desenhada para admitir loadAsync na FASE 4 (via eng::jobs), mas
///   NENHUMA função async existe aqui (missão R14).
/// - Handles seguram shared_ptr: unload remove do cache, dados vivem
///   enquanto houver handles (sem dangling silencioso).
/// - Loaders registrados POR TIPO C++ (TypeTag — mesmo padrão de
///   ecs/events, sem RTTI). JsonAssetLoader serve Scene/Prefab/Json
///   devolvendo serial::JsonValue (a interpretação estrutural de cena é
///   do SceneSerializer em eng::scene — desvio D4 da auditoria).
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "eng/assets/AssetId.hpp"
#include "eng/assets/AssetMeta.hpp"
#include "eng/assets/AssetRegistry.hpp"
#include "eng/assets/AssetResolver.hpp"
#include "eng/assets/AssetType.hpp"
#include "eng/core/Result.hpp"
#include "eng/fs/FileSystem.hpp"
#include "eng/serial/JsonValue.hpp"

namespace eng::assets {

// =============================================================================
// Loader por tipo — interface template (chave sem RTTI, padrão TypeTag)
// =============================================================================

/// Interface de loader para o tipo T: bytes (meta) → objeto T.
template<typename T>
class IAssetLoaderFor {
public:
    virtual ~IAssetLoaderFor() = default;

    /// Este loader aceita o AssetType do meta? (JsonAssetLoader aceita
    /// Scene/Prefab/Json — o conteúdo é JSON em todos.)
    [[nodiscard]] virtual bool supports(AssetType type) const = 0;

    /// Decodifica os bytes. Erros: ParseError claro; NUNCA lança.
    [[nodiscard]] virtual eng::core::Result<std::shared_ptr<T>> load(
        const AssetMeta& meta, std::span<const std::byte> bytes) const = 0;
};

namespace detail {

template<typename T>
struct LoaderTag {
    static constexpr char token = 0;
};

template<typename T>
[[nodiscard]] const void* loaderKey() noexcept
{
    return &LoaderTag<T>::token;
}

/// Entrada type-erased do mapa de loaders: o shared_ptr<void> segura o
/// IAssetLoaderFor<T> concreto; os ponteiros-de-função reencaminham com o
/// cast correto (garantido pela CHAVE do mapa — mesmo padrão de
/// ecs/events, sem RTTI).
struct LoaderEntry {
    std::shared_ptr<void> loader;
    bool (*supports)(const LoaderEntry&, AssetType) = nullptr;
    eng::core::Result<std::shared_ptr<void>> (*load)(
        const LoaderEntry&, const AssetMeta&,
        std::span<const std::byte>) = nullptr;
};

template<typename T>
[[nodiscard]] LoaderEntry makeLoaderEntry(
    std::shared_ptr<IAssetLoaderFor<T>> concrete)
{
    LoaderEntry entry;
    entry.loader = std::static_pointer_cast<void>(concrete);
    entry.supports = [](const LoaderEntry& e, AssetType type) {
        return static_cast<const IAssetLoaderFor<T>*>(e.loader.get())
            ->supports(type);
    };
    entry.load = [](const LoaderEntry& e, const AssetMeta& meta,
                    std::span<const std::byte> bytes)
        -> eng::core::Result<std::shared_ptr<void>> {
        auto decoded =
            static_cast<const IAssetLoaderFor<T>*>(e.loader.get())
                ->load(meta, bytes);
        if (decoded.isError()) {
            return eng::core::makeUnexpected(decoded.error());
        }
        return std::static_pointer_cast<void>(decoded.value());
    };
    return entry;
}

} // namespace detail

/// Loader de JSON para os tipos textuais (Scene/Prefab/Json) — devolve o
/// JsonValue PARSEADO com limites; a semântica é do consumidor (D4).
class JsonAssetLoader final : public IAssetLoaderFor<eng::serial::JsonValue> {
public:
    [[nodiscard]] bool supports(AssetType type) const override;
    [[nodiscard]] eng::core::Result<std::shared_ptr<eng::serial::JsonValue>>
    load(const AssetMeta& meta,
         std::span<const std::byte> bytes) const override;
};

// =============================================================================
// Handle
// =============================================================================

template<typename T>
class AssetHandle {
public:
    AssetHandle() = default;

    [[nodiscard]] AssetId id() const noexcept { return id_; }
    [[nodiscard]] T* get() const noexcept { return data_.get(); }
    [[nodiscard]] T& operator*() const noexcept { return *data_; }
    [[nodiscard]] T* operator->() const noexcept { return data_.get(); }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return data_ != nullptr;
    }

private:
    friend class AssetManager;

    AssetHandle(AssetId id, std::shared_ptr<T> data)
        : id_(id), data_(std::move(data))
    {
    }

    AssetId id_{};
    std::shared_ptr<T> data_;
};

// =============================================================================
// Manager
// =============================================================================

class AssetManager final {
public:
    /// `fs` precisa sobreviver ao manager (referência não-dona).
    AssetManager(const AssetRegistry& registry, AssetResolver resolver,
                 const eng::fs::FileSystem& fs);

    /// Registra o loader do tipo T (substitui se já existia).
    template<typename T>
    void registerLoader(std::shared_ptr<IAssetLoaderFor<T>> loader)
    {
        loaders_[detail::loaderKey<T>()] =
            detail::makeLoaderEntry(std::move(loader));
    }

    /// Carrega (ou devolve do cache) o asset T do id.
    /// Erros: não registrado / traversal / I/O / parse — sempre Result.
    template<typename T>
    [[nodiscard]] eng::core::Result<AssetHandle<T>> load(AssetId id)
    {
        if (const auto cached = getLoaded<T>(id); cached.has_value()) {
            return cached.value();
        }

        const AssetMeta* meta = registry_.find(id);
        if (meta == nullptr) {
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::NotFound,
                "AssetManager::load: asset " + id.toString() +
                    " não está no registry"});
        }

        const auto it = loaders_.find(detail::loaderKey<T>());
        if (it == loaders_.end()) {
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::NotSupported,
                "AssetManager::load: nenhum loader registrado para o tipo C++ "
                "requisitado (asset " +
                    id.toString() + ")"});
        }
        if (!it->second.supports(it->second, meta->type)) {
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::NotSupported,
                "AssetManager::load: loader não suporta AssetType " +
                    std::string(assetTypeName(meta->type)) + " (asset " +
                    id.toString() + ")"});
        }

        const auto bytes = resolver_.read(id, fs_);
        if (bytes.isError()) {
            return eng::core::makeUnexpected(bytes.error());
        }

        auto decoded = it->second.load(it->second, *meta, bytes.value());
        if (decoded.isError()) {
            return eng::core::makeUnexpected(decoded.error());
        }

        std::shared_ptr<T> data =
            std::static_pointer_cast<T>(decoded.value());
        CacheEntry entry{detail::loaderKey<T>(),
                         std::static_pointer_cast<void>(data)};
        cache_[id] = std::move(entry);
        return AssetHandle<T>{id, std::move(data)};
    }

    /// Handle já carregado (nullopt se não está no cache OU o tipo T não
    /// é o tipo com que foi carregado — guarda de tipo sem RTTI).
    template<typename T>
    [[nodiscard]] std::optional<AssetHandle<T>> getLoaded(AssetId id) const
    {
        const auto it = cache_.find(id);
        if (it == cache_.end() ||
            it->second.typeKey != detail::loaderKey<T>()) {
            return std::nullopt;
        }
        return AssetHandle<T>{
            id, std::static_pointer_cast<T>(it->second.data)};
    }

    /// Remove do cache (handles vivos seguram os dados; próximos load()
    /// releem do disco).
    void unload(AssetId id) { cache_.erase(id); }

    [[nodiscard]] std::size_t loadedCount() const noexcept
    {
        return cache_.size();
    }

private:
    struct CacheEntry {
        const void* typeKey = nullptr;   ///< loaderKey<T>() da carga
        std::shared_ptr<void> data;      ///< objeto apagado de tipo
    };

    const AssetRegistry& registry_;
    AssetResolver resolver_;
    const eng::fs::FileSystem& fs_;
    std::unordered_map<const void*, detail::LoaderEntry> loaders_;
    std::unordered_map<AssetId, CacheEntry> cache_;
};

} // namespace eng::assets
