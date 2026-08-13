#pragma once

#include <exception>
#include <string>
#include <utility>

namespace urk::guard {

template <typename Callable> bool InvokeCpp(Callable &&callable, std::string *error) noexcept {
    if (error)
        error->clear();
    try {
        std::forward<Callable>(callable)();
        return true;
    } catch (const std::exception &exception) {
        if (error) {
            try {
                *error = exception.what();
            } catch (...) {
                error->clear();
            }
        }
    } catch (...) {
        if (error) {
            try {
                *error = "unknown C++ exception";
            } catch (...) {
                error->clear();
            }
        }
    }
    return false;
}

} // namespace urk::guard
