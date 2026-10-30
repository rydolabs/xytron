#include "core/object_registry.hpp"

namespace nativeweb {
namespace detail {

ObjectRegistry::ObjectRegistry()
    : nextId_(1)
{
}

bool ObjectRegistry::contains(ObjectId id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return objects_.find(id) != objects_.end();
}

bool ObjectRegistry::release(ObjectId id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::map<ObjectId, EntryPtr>::iterator found = objects_.find(id);
    if (found == objects_.end())
        return false;

    objects_.erase(found);
    return true;
}

void ObjectRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    objects_.clear();
}

std::size_t ObjectRegistry::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return objects_.size();
}

} // namespace detail
} // namespace nativeweb
