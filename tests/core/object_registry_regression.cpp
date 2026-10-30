#include "core/object_registry.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

int failures = 0;

void fail(const char* expression, const char* file, int line)
{
    std::cerr << file << ":" << line << ": CHECK failed: "
              << expression << std::endl;
    ++failures;
}

#define CHECK(expr) do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

struct Camera
{
    explicit Camera(int cameraId)
        : id(cameraId)
    {
    }

    int id;
};

struct Radar
{
    int id;
};

struct LifetimeProbe
{
    explicit LifetimeProbe(int* destroyedCount)
        : destroyed(destroyedCount)
    {
    }

    ~LifetimeProbe()
    {
        ++(*destroyed);
    }

    int* destroyed;
};

void testAddGetRelease()
{
    nativeweb::detail::ObjectRegistry registry;

    std::shared_ptr<Camera> camera(new Camera(7));
    const nativeweb::detail::ObjectId id = registry.add(camera);

    CHECK(id != 0);
    CHECK(registry.size() == 1u);
    CHECK(registry.contains(id));

    std::shared_ptr<Camera> fromRegistry = registry.get<Camera>(id);
    CHECK(static_cast<bool>(fromRegistry));
    CHECK(fromRegistry->id == 7);
    CHECK(fromRegistry.get() == camera.get());

    CHECK(registry.release(id));
    CHECK(!registry.contains(id));
    CHECK(registry.size() == 0u);
    CHECK(!registry.release(id));
}

void testUniqueIdsAndTypeSafety()
{
    nativeweb::detail::ObjectRegistry registry;

    const nativeweb::detail::ObjectId cameraId =
        registry.add(std::shared_ptr<Camera>(new Camera(1)));

    const nativeweb::detail::ObjectId radarId =
        registry.add(std::shared_ptr<Radar>(new Radar()));

    CHECK(cameraId != radarId);
    CHECK(radarId > cameraId);

    CHECK(static_cast<bool>(registry.get<Camera>(cameraId)));
    CHECK(!registry.get<Radar>(cameraId));
    CHECK(!registry.get<Camera>(999999));
}

void testRegistryOwnsLifetime()
{
    int destroyed = 0;
    nativeweb::detail::ObjectRegistry registry;

    std::shared_ptr<LifetimeProbe> object(
        new LifetimeProbe(&destroyed));

    const nativeweb::detail::ObjectId id = registry.add(object);

    object.reset();
    CHECK(destroyed == 0);

    CHECK(registry.release(id));
    CHECK(destroyed == 1);
}

void testClearReleasesObjects()
{
    int destroyed = 0;
    nativeweb::detail::ObjectRegistry registry;

    registry.add(std::shared_ptr<LifetimeProbe>(
        new LifetimeProbe(&destroyed)));

    registry.add(std::shared_ptr<LifetimeProbe>(
        new LifetimeProbe(&destroyed)));

    CHECK(registry.size() == 2u);

    registry.clear();

    CHECK(registry.size() == 0u);
    CHECK(destroyed == 2);
}

void testOwnerLeaseFollowsManagedObject()
{
    int objectDestroyed = 0;
    int ownerDestroyed = 0;

    nativeweb::detail::ObjectRegistry registry;

    std::shared_ptr<LifetimeProbe> object(
        new LifetimeProbe(
            &objectDestroyed));

    std::shared_ptr<LifetimeProbe> owner(
        new LifetimeProbe(
            &ownerDestroyed));

    const nativeweb::detail::ObjectId id =
        registry.add(
            object,
            std::static_pointer_cast<void>(
                owner));

    std::shared_ptr<LifetimeProbe> managed =
        registry.get<LifetimeProbe>(id);

    object.reset();
    owner.reset();

    CHECK(objectDestroyed == 0);
    CHECK(ownerDestroyed == 0);

    // Removing the registry entry must not invalidate a managed pointer that
    // was already handed out. Its aliasing state retains both object and owner.
    CHECK(registry.release(id));
    CHECK(objectDestroyed == 0);
    CHECK(ownerDestroyed == 0);

    managed.reset();

    CHECK(objectDestroyed == 1);
    CHECK(ownerDestroyed == 1);
}

void testRejectNullObject()
{
    nativeweb::detail::ObjectRegistry registry;
    std::shared_ptr<Camera> empty;

    bool threw = false;

    try
    {
        (void)registry.add(empty);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    CHECK(threw);
}

} // namespace

int main()
{
    testAddGetRelease();
    testUniqueIdsAndTypeSafety();
    testRegistryOwnsLifetime();
    testClearReleasesObjects();
    testOwnerLeaseFollowsManagedObject();
    testRejectNullObject();

    if (failures != 0)
    {
        std::cerr << "Object registry regression failed: "
                  << failures << " check(s)" << std::endl;
        return 1;
    }

    std::cout << "Object registry regression: PASS" << std::endl;
    return 0;
}
