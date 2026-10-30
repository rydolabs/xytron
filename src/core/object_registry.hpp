#ifndef NATIVEWEB_OBJECT_REGISTRY_HPP_INCLUDED
#define NATIVEWEB_OBJECT_REGISTRY_HPP_INCLUDED

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>

namespace nativeweb {
namespace detail {

typedef std::uint64_t ObjectId;

class ObjectRegistry
{
public:
    ObjectRegistry();

    template <typename T>
    ObjectId add(const std::shared_ptr<T>& object)
    {
        return add(
            object,
            std::shared_ptr<void>());
    }

    template <typename T>
    ObjectId add(
        const std::shared_ptr<T>& object,
        const std::shared_ptr<void>& owner)
    {
        if (!object)
            throw std::invalid_argument(
                "NativeWeb object registry cannot store a null object");

        const ObjectId id =
            nextId_.fetch_add(1);

        const EntryPtr entry(
            new Entry(
                std::static_pointer_cast<void>(
                    object),
                std::type_index(typeid(T)),
                owner));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            objects_.insert(
                std::make_pair(
                    id,
                    entry));
        }

        return id;
    }

    template <typename T>
    std::shared_ptr<T> get(ObjectId id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::map<ObjectId, EntryPtr>::const_iterator found =
            objects_.find(id);

        if (found == objects_.end())
            return std::shared_ptr<T>();

        if (
            found->second->type !=
            std::type_index(typeid(T)))
        {
            return std::shared_ptr<T>();
        }

        // The aliasing shared_ptr keeps the complete Entry alive. Therefore a
        // managed object obtained from the registry also keeps its optional
        // module/owner lease alive even after the registry entry is released.
        return std::shared_ptr<T>(
            found->second,
            static_cast<T*>(
                found->second->object.get()));
    }

    bool contains(ObjectId id) const;
    bool release(ObjectId id);
    void clear();

    std::size_t size() const;

private:
    struct Entry
    {
        Entry(
            const std::shared_ptr<void>& value,
            const std::type_index& valueType,
            const std::shared_ptr<void>& valueOwner)
            : object(value),
              type(valueType),
              owner(valueOwner)
        {
        }

        std::shared_ptr<void> object;
        std::type_index type;
        std::shared_ptr<void> owner;
    };

    typedef std::shared_ptr<Entry> EntryPtr;

    mutable std::mutex mutex_;
    std::map<ObjectId, EntryPtr> objects_;
    std::atomic<ObjectId> nextId_;
};

} // namespace detail
} // namespace nativeweb

#endif
