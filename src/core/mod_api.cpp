#include "mod_api_internal.h"

#include <algorithm>
#include <cstring>

namespace URK::ModApiInternal {

int CopyName(const std::string &value, char *output, size_t size) {
    if (!output || !size || value.empty())
        return 0;
    const size_t count = (std::min)(value.size(), size - 1);
    std::memcpy(output, value.data(), count);
    output[count] = '\0';
    return 1;
}

} // namespace URK::ModApiInternal

const URK_MonoApi *ModApi_Mono(MonoApi *api) {
    return URK::ModApiInternal::BuildMonoApiTable(api);
}
