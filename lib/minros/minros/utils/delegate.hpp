#pragma once
#include <type_traits>

namespace minros::utils {
    
template<typename Ret = void, typename... Args>
class delegate {
public:
    using Fn = Ret (*)(Args..., void*);

    delegate() = default;
    delegate(Fn fn, void* obj) : fn(fn), obj(obj) {}

    Ret operator()(Args... args) const {
        if (fn) return fn(args..., obj);
        if constexpr (!std::is_void_v<Ret>) return Ret{};
    }

    bool is_valid() const { return fn != nullptr; }

private:
    Fn    fn  = nullptr;
    void* obj = nullptr;
};

} // namespace minros::utils


