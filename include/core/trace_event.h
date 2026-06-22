#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Core {

enum class TraceEventType : std::uint8_t {
    GoalDetected,
    ToolStarted,
    ToolCompleted,
    EvidenceAdded,
    OutcomeComputed,
    AICalled
};

struct TraceEvent {
    TraceEventType type;
    std::string tool;
    std::string args;
    std::vector<std::string> files;
    std::string result;
    double confidence{0.0};
};

inline const char *trace_event_type_name(TraceEventType t) {
    switch (t) {
        case TraceEventType::GoalDetected:    return "goal";
        case TraceEventType::ToolStarted:     return "tool_start";
        case TraceEventType::ToolCompleted:   return "tool_complete";
        case TraceEventType::EvidenceAdded:   return "evidence";
        case TraceEventType::OutcomeComputed: return "outcome";
        case TraceEventType::AICalled:        return "ai";
    }
    return "unknown";
}

} // namespace Core
