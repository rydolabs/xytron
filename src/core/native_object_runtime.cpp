#include "core/native_object_runtime.hpp"

#include "nativeweb/error.hpp"

namespace nativeweb {
namespace detail {

NativeObjectRuntime::NativeObjectRuntime()
{
}

NativeObjectHandle NativeObjectRuntime::add(
    const std::shared_ptr<void>& object,
    const std::string& typeName)
{
    return add(
        object,
        typeName,
        std::shared_ptr<void>());
}

NativeObjectHandle NativeObjectRuntime::add(
    const std::shared_ptr<void>& object,
    const std::string& typeName,
    const std::shared_ptr<void>& owner)
{
    if (!object)
        throw Error(
            "invalid_native_object",
            "NativeWeb cannot register a null native object");

    if (typeName.empty())
        throw Error(
            "invalid_native_object",
            "NativeWeb native object type name cannot be empty");

    const ObjectId id =
        objects_.add<void>(
            object,
            owner);

    Entry entry;
    entry.typeName = typeName;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[id] = entry;
    }

    return NativeObjectHandle(id, typeName);
}

void NativeObjectRuntime::bindMethod(
    const NativeObjectHandle& handle,
    const std::string& method,
    const DynamicFunction& function)
{
    if (!function)
        throw Error(
            "invalid_native_object_method",
            "Native object method is invalid");

    bindManagedMethod(
        handle,
        method,
        [function](const std::shared_ptr<void>&, const VariantList& args) {
            return function(args);
        });
}

void NativeObjectRuntime::bindManagedMethod(
    const NativeObjectHandle& handle,
    const std::string& method,
    const ManagedObjectMethod& function)
{
    if (!handle.valid())
        throw Error(
            "invalid_native_object",
            "Native object handle is invalid");

    if (method.empty() || !function)
        throw Error(
            "invalid_native_object_method",
            "Native object method is invalid");

    std::lock_guard<std::mutex> lock(mutex_);
    std::map<ObjectId, Entry>::iterator found = entries_.find(handle.id);

    if (found == entries_.end())
        throw Error(
            "native_object_not_found",
            "Native object no longer exists");

    if (found->second.typeName != handle.type)
        throw Error(
            "native_object_type_mismatch",
            "Native object type does not match handle");

    found->second.methods[method] = function;
}

Any NativeObjectRuntime::call(
    const NativeObjectHandle& handle,
    const std::string& method,
    const VariantList& args)
{
    ManagedObjectMethod function;
    std::shared_ptr<void> object;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::map<ObjectId, Entry>::const_iterator found = entries_.find(handle.id);
        if (found == entries_.end())
            throw Error(
                "native_object_not_found",
                "Native object no longer exists");

        if (found->second.typeName != handle.type)
            throw Error(
                "native_object_type_mismatch",
                "Native object type does not match handle");

        std::map<std::string, ManagedObjectMethod>::const_iterator methodIt =
            found->second.methods.find(method);
        if (methodIt == found->second.methods.end())
            throw Error(
                "native_object_method_not_found",
                "Native object method not found: " + method);

        // Acquire the registry object's lease under the same lock that guards
        // release(). This prevents dispatch against a destroyed plugin object.
        object = objects_.get<void>(handle.id);
        if (!object)
            throw Error(
                "native_object_not_found",
                "Native object no longer exists");
        function = methodIt->second;
    }

    return function(object, args);
}

bool NativeObjectRuntime::release(
    const NativeObjectHandle& handle)
{
    bool erased = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::map<ObjectId, Entry>::iterator found =
            entries_.find(handle.id);

        if (found == entries_.end())
            return false;

        if (found->second.typeName != handle.type)
            return false;

        entries_.erase(found);
        erased = true;
    }

    if (erased)
        objects_.release(handle.id);

    return erased;
}

void NativeObjectRuntime::clear()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

    objects_.clear();
}

std::size_t NativeObjectRuntime::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

} // namespace detail
} // namespace nativeweb
