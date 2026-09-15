#include "eng/fs/Path.hpp"

namespace eng::fs {

Path::Path(std::string_view generic)
    : native_(generic.begin(), generic.end())
{
}

Path::Path(FromNative, std::filesystem::path native)
    : native_(std::move(native))
{
}

Path Path::fromNative(std::filesystem::path native)
{
    return Path(FromNative{}, std::move(native));
}

Path Path::operator/(const Path& tail) const
{
    return fromNative(native_ / tail.native_);
}

Path& Path::operator/=(const Path& tail)
{
    native_ /= tail.native_;
    return *this;
}

Path Path::parent() const
{
    return fromNative(native_.parent_path());
}

Path Path::filename() const
{
    return fromNative(native_.filename());
}

Path Path::stem() const
{
    return fromNative(native_.stem());
}

Path Path::extension() const
{
    return fromNative(native_.extension());
}

Path Path::normalized() const
{
    return fromNative(native_.lexically_normal());
}

bool Path::isAbsolute() const noexcept
{
    return native_.is_absolute();
}

bool Path::isEmpty() const noexcept
{
    return native_.empty();
}

bool Path::valid() const noexcept
{
    const std::string text = str();
    return !text.empty() && text.find('\0') == std::string::npos;
}

bool Path::isWithin(const Path& root) const
{
    const std::filesystem::path me = native_.lexically_normal();
    const std::filesystem::path base = root.native_.lexically_normal();

    // Componentes de base têm que ser prefixo COMPONENTE a COMPONENTE de me.
    auto meIt = me.begin();
    for (const auto& component : base) {
        if (meIt == me.end() || *meIt != component) {
            return false;
        }
        ++meIt;
    }
    return true;
}

std::string Path::str() const
{
    return native_.generic_string();
}

const std::filesystem::path& Path::native() const noexcept
{
    return native_;
}

} // namespace eng::fs
