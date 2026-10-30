#ifndef NATIVEWEB_PROCESS_BROKER_HPP_INCLUDED
#define NATIVEWEB_PROCESS_BROKER_HPP_INCLUDED

#include "nativeweb/any.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nativeweb { namespace detail {

// EXPERIMENTAL: trusted native host -> preapproved local worker, NOT a browser
// API, permission evaluator, sandbox, or plugin loader. NEVER forward raw JS
// operations here without engine-attested document/frame NW-014 authorization.
// One broker = one owner/session = one child process. JSONL v0 only.
class ProcessBroker {
public:
    ProcessBroker(const std::vector<std::string>& command, unsigned timeoutMs = 5000);
    ~ProcessBroker();
    ProcessBroker(const ProcessBroker&) = delete;
    ProcessBroker& operator=(const ProcessBroker&) = delete;

    Any invoke(const std::string& operation, const VariantList& args);
    void stop();
    bool running() const;

private:
    struct Process;
    void stopLocked();
    std::string exchange(const std::string& data);
    std::unique_ptr<Process> process_;
    mutable std::mutex mutex_;
    unsigned timeoutMs_;
    std::int32_t nextId_;
    bool stopped_;
};

}} // namespace nativeweb::detail
#endif
