// mod/ui/highlight.h for Unreal: screen-space only (no camera API yet).

std::string UnrealHighlightModule() {
    return R"URK(#pragma once

#include <cstdint>
#include <imgui.h>
#include <mutex>
#include <string>
#include <vector>

namespace ModUI::Highlight {
using HighlightId = std::uint32_t;

struct Style {
    ImU32 color = IM_COL32(255, 196, 0, 255);
    float thickness = 2.0f;
};

class Manager {
  public:
    HighlightId add_screen_rect(ImVec2 min, ImVec2 max, const char *label = nullptr, Style style = {}) {
        std::lock_guard lock(mutex_);
        const HighlightId id = ++next_;
        entries_.push_back({id, min, max, label ? label : "", style});
        return id;
    }

    bool remove(HighlightId id) {
        std::lock_guard lock(mutex_);
        const std::size_t before = entries_.size();
        std::erase_if(entries_, [id](const Entry &entry) { return entry.id == id; });
        return entries_.size() != before;
    }

    void clear() {
        std::lock_guard lock(mutex_);
        entries_.clear();
    }

    // Called by the render hook inside an ImGui frame.
    void render() {
        std::lock_guard lock(mutex_);
        ImDrawList *draw = ImGui::GetForegroundDrawList();
        for (const Entry &entry : entries_) {
            draw->AddRect(entry.min, entry.max, entry.style.color, 0.0f, 0, entry.style.thickness);
            if (!entry.label.empty())
                draw->AddText(ImVec2(entry.min.x, entry.min.y - ImGui::GetTextLineHeight()), entry.style.color,
                              entry.label.c_str());
        }
    }

  private:
    struct Entry {
        HighlightId id;
        ImVec2 min;
        ImVec2 max;
        std::string label;
        Style style;
    };

    std::mutex mutex_;
    std::vector<Entry> entries_;
    HighlightId next_ = 0;
};

inline Manager &manager() {
    static Manager instance;
    return instance;
}

inline HighlightId add_screen_rect(ImVec2 min, ImVec2 max, const char *label = nullptr, Style style = {}) {
    return manager().add_screen_rect(min, max, label, style);
}
inline bool remove(HighlightId id) {
    return manager().remove(id);
}
inline void clear() {
    manager().clear();
}
} // namespace ModUI::Highlight
)URK";
}
