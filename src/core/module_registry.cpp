#include "core/module_registry.hpp"
#include "nativeweb/error.hpp"

#include <cctype>
#include <utility>

namespace nativeweb {
namespace detail {
namespace {

bool validModuleId(const std::string& value)
{
    if (value.empty() || value.size() > 80)
        return false;

    if (!(value[0] >= 'a' && value[0] <= 'z'))
        return false;

    for (std::size_t i = 1; i < value.size(); ++i)
    {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    }

    return value != "xytron" &&
           value != "native" &&
           value != "invoke" &&
           value != "on" &&
           value != "off" &&
           value != "then" &&
           value != "constructor" &&
           value != "prototype";
}

} // namespace

ModuleRegistry::ModuleRegistry() : state_(new State()) {}

ModuleRegistration ModuleRegistry::registerModule(
    const ModuleDescriptor& descriptor)
{
    if (!validModuleId(descriptor.id))
        throw Error("invalid_module_id",
                    "Invalid or reserved module id: " + descriptor.id);

    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->modules.count(descriptor.id))
        throw Error("duplicate_module",
                    "Module id already registered: " + descriptor.id);

    if (descriptor.role == ModuleRole::Main &&
        !state_->mainId.empty())
        throw Error("duplicate_main",
                    "The application already has a Main Backend");

    const std::uint64_t generation = state_->nextGeneration++;
    state_->modules.insert(std::make_pair(
        descriptor.id, Entry(descriptor, generation)));
    if (descriptor.role == ModuleRole::Main)
        state_->mainId = descriptor.id;

    return ModuleRegistration(state_, descriptor.id, generation);
}

bool ModuleRegistry::release(const std::shared_ptr<State>& state,
                             const std::string& id,
                             std::uint64_t generation)
{
    if (!state)
        return false;

    std::lock_guard<std::mutex> lock(state->mutex);
    std::map<std::string, Entry>::iterator it = state->modules.find(id);
    if (it == state->modules.end() ||
        it->second.generation != generation)
        return false;
    if (state->mainId == id)
        state->mainId.clear();
    state->modules.erase(it);
    return true;
}

bool ModuleRegistry::unregisterModule(const std::string& id)
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::map<std::string, Entry>::iterator it =
        state_->modules.find(id);
    if (it == state_->modules.end())
        return false;

    if (state_->mainId == id)
        state_->mainId.clear();
    state_->modules.erase(it);
    return true;
}

bool ModuleRegistry::has(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->modules.count(id) != 0;
}

std::uint64_t ModuleRegistry::generation(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    const std::map<std::string, Entry>::const_iterator it =
        state_->modules.find(id);
    return it == state_->modules.end() ? 0 : it->second.generation;
}

bool ModuleRegistry::find(const std::string& id,
                          ModuleDescriptor* output) const
{
    if (!output)
        return false;

    std::lock_guard<std::mutex> lock(state_->mutex);
    const std::map<std::string, Entry>::const_iterator it =
        state_->modules.find(id);
    if (it == state_->modules.end())
        return false;
    *output = it->second.descriptor;
    return true;
}

bool ModuleRegistry::hasMain() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return !state_->mainId.empty();
}

std::string ModuleRegistry::mainId() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->mainId;
}

std::vector<ModuleDescriptor> ModuleRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<ModuleDescriptor> out;
    out.reserve(state_->modules.size());
    for (std::map<std::string, Entry>::const_iterator it =
             state_->modules.begin(); it != state_->modules.end(); ++it)
        out.push_back(it->second.descriptor);
    return out;
}

std::size_t ModuleRegistry::size() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->modules.size();
}

void ModuleRegistry::clear()
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->modules.clear();
    state_->mainId.clear();
}

ModuleRegistration::ModuleRegistration() : generation_(0) {}

ModuleRegistration::ModuleRegistration(
    const std::shared_ptr<ModuleRegistry::State>& state,
    const std::string& id,
    std::uint64_t generation)
    : state_(state), id_(id), generation_(generation)
{
}

ModuleRegistration::~ModuleRegistration() { release(); }

ModuleRegistration::ModuleRegistration(ModuleRegistration&& other)
    : state_(std::move(other.state_)),
      id_(std::move(other.id_)),
      generation_(other.generation_)
{
    other.generation_ = 0;
}

ModuleRegistration& ModuleRegistration::operator=(ModuleRegistration&& other)
{
    if (this != &other)
    {
        release();
        state_ = std::move(other.state_);
        id_ = std::move(other.id_);
        generation_ = other.generation_;
        other.generation_ = 0;
    }
    return *this;
}

bool ModuleRegistration::valid() const
{
    return generation_ != 0 && !state_.expired();
}

bool ModuleRegistration::release()
{
    if (generation_ == 0)
        return false;

    const std::shared_ptr<ModuleRegistry::State> state = state_.lock();
    const bool removed = ModuleRegistry::release(
        state, id_, generation_);
    state_.reset();
    id_.clear();
    generation_ = 0;
    return removed;
}

} // namespace detail
} // namespace nativeweb
