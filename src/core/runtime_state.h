#pragma once

#include <windows.h>

#include <chrono>
#include <string_view>

enum class RuntimeReadiness {
    None,
    ModuleSeen,
    ExportsResolved,
    RuntimeInitialized,
    DomainAvailable,
    LoaderThreadAttached,
    AssembliesStable,
    ModsAllowed,
    SdkAllowed,
};

class RuntimeState {
  public:
    explicit RuntimeState(std::string_view backend);

    void Reset();
    void Transition(RuntimeReadiness state, const void *domain, std::string_view source);
    bool AtLeast(RuntimeReadiness state) const;
    long long ElapsedMs() const;
    const char *DomainSource() const;

  private:
    std::string_view backend_;
    unsigned reached_ = 0;
    std::chrono::steady_clock::time_point started_{};
    const char *domainSource_ = "none";
};
