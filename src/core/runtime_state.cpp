#include "runtime_state.h"

#include "logger.h"

namespace {
const char *StateName(RuntimeReadiness state) {
    switch (state) {
        case RuntimeReadiness::ModuleSeen:
            return "ModuleSeen";
        case RuntimeReadiness::ExportsResolved:
            return "ExportsResolved";
        case RuntimeReadiness::RuntimeInitialized:
            return "RuntimeInitialized";
        case RuntimeReadiness::DomainAvailable:
            return "DomainAvailable";
        case RuntimeReadiness::LoaderThreadAttached:
            return "LoaderThreadAttached";
        case RuntimeReadiness::AssembliesStable:
            return "AssembliesStable";
        case RuntimeReadiness::ModsAllowed:
            return "ModsAllowed";
        case RuntimeReadiness::SdkAllowed:
            return "SdkAllowed";
        default:
            return "None";
    }
}
} // namespace

RuntimeState::RuntimeState(std::string_view backend) : backend_(backend) {
    Reset();
}

void RuntimeState::Reset() {
    reached_ = 0;
    started_ = std::chrono::steady_clock::now();
    domainSource_ = "none";
}

void RuntimeState::Transition(RuntimeReadiness state, const void *domain, std::string_view source) {
    const unsigned bit = 1u << static_cast<unsigned>(state);
    if (reached_ & bit)
        return;
    reached_ |= bit;
    if (state == RuntimeReadiness::DomainAvailable && !source.empty())
        domainSource_ = source.data();
    Log("[readiness] pid=%lu tid=%lu backend=%.*s state=%s domain=%p source=%s "
        "elapsed=%lldms",
        GetCurrentProcessId(), GetCurrentThreadId(), static_cast<int>(backend_.size()), backend_.data(),
        StateName(state), domain, domainSource_, ElapsedMs());
}

bool RuntimeState::AtLeast(RuntimeReadiness state) const {
    return (reached_ & (1u << static_cast<unsigned>(state))) != 0;
}

long long RuntimeState::ElapsedMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
}

const char *RuntimeState::DomainSource() const {
    return domainSource_;
}
