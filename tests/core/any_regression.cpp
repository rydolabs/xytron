#include "nativeweb/any.hpp"
#include "nativeweb/types.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace {

int failures = 0;

void fail(const char* expression, const char* file, int line)
{
    std::cerr << file << ":" << line << ": CHECK failed: "
              << expression << std::endl;
    ++failures;
}

#define CHECK(expr) do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

template <typename Function>
void checkRuntimeError(Function function)
{
    bool threw = false;
    try
    {
        function();
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    catch (...)
    {
        std::cerr << "unexpected exception type" << std::endl;
        ++failures;
        return;
    }

    CHECK(threw);
}

void testEmptyAndScalarValues()
{
    Any empty;
    CHECK(empty.empty());
    CHECK(empty.type() == typeid(void));

    Any integer(42);
    CHECK(!integer.empty());
    CHECK(integer.type() == typeid(int));
    CHECK(AnyCast<int>(integer) == 42);
    CHECK(static_cast<int>(integer) == 42);

    Any floating(3.5);
    CHECK(floating.type() == typeid(double));
    CHECK(AnyCast<double>(floating) == 3.5);

    Any boolean(true);
    CHECK(boolean.type() == typeid(bool));
    CHECK(AnyCast<bool>(boolean));

    Any text("nativeweb");
    CHECK(text.type() == typeid(std::string));
    CHECK(AnyCast<std::string>(text) == "nativeweb");
    CHECK(static_cast<std::string>(text) == "nativeweb");
}

void testAssignmentAndCopy()
{
    Any value;
    value = 7;
    CHECK(AnyCast<int>(value) == 7);

    Any copied(value);
    CHECK(AnyCast<int>(copied) == 7);

    copied = std::string("changed");
    CHECK(AnyCast<std::string>(copied) == "changed");
    CHECK(AnyCast<int>(value) == 7);

    Any assigned;
    assigned = copied;
    CHECK(AnyCast<std::string>(assigned) == "changed");
}

void testVariantList()
{
    VariantList list;
    list.push_back(Any(1));
    list.push_back(Any("two"));
    list.push_back(Any(true));

    Any value(list);
    CHECK(value.type() == typeid(VariantList));
    CHECK(AnyCast<VariantList>(value).size() == 3u);
    CHECK(AnyCast<int>(value[0]) == 1);
    CHECK(AnyCast<std::string>(value[1]) == "two");
    CHECK(AnyCast<bool>(value[2]));

    const Any constValue(value);
    CHECK(AnyCast<int>(constValue[0]) == 1);
}

void testVariantDict()
{
    VariantDict dict;
    dict["answer"] = 42;
    dict["name"] = "NativeWeb";
    dict["enabled"] = true;

    Any value(dict);
    CHECK(value.type() == typeid(VariantDict));
    CHECK(AnyCast<int>(value["answer"]) == 42);
    CHECK(AnyCast<std::string>(value["name"]) == "NativeWeb");
    CHECK(AnyCast<bool>(value["enabled"]));

    const Any constValue(value);
    CHECK(AnyCast<int>(constValue["answer"]) == 42);
}

void testNestedValues()
{
    VariantList channels;
    channels.push_back(Any(1));
    channels.push_back(Any(2));
    channels.push_back(Any(3));

    VariantDict device;
    device["name"] = "camera";
    device["retry"] = true;
    device["channels"] = channels;

    Any root(device);
    CHECK(AnyCast<std::string>(root["name"]) == "camera");
    CHECK(AnyCast<bool>(root["retry"]));
    CHECK(AnyCast<int>(root["channels"][1]) == 2);
}

void testBinaryStorage()
{
    nativeweb::Binary bytes;
    bytes.push_back(0x00);
    bytes.push_back(0x7f);
    bytes.push_back(0xff);

    Any value(bytes);
    CHECK(value.type() == typeid(nativeweb::Binary));

    const nativeweb::Binary& stored =
        AnyCast<const nativeweb::Binary&>(value);
    CHECK(stored.size() == 3u);
    CHECK(stored[0] == 0x00);
    CHECK(stored[1] == 0x7f);
    CHECK(stored[2] == 0xff);
}

void testInvalidAccess()
{
    Any integer(42);

    checkRuntimeError([&integer]() {
        (void)AnyCast<std::string>(integer);
    });

    checkRuntimeError([&integer]() {
        (void)integer[0];
    });

    checkRuntimeError([&integer]() {
        (void)integer[std::string("key")];
    });
}

} // namespace

int main()
{
    testEmptyAndScalarValues();
    testAssignmentAndCopy();
    testVariantList();
    testVariantDict();
    testNestedValues();
    testBinaryStorage();
    testInvalidAccess();

    if (failures != 0)
    {
        std::cerr << "Any regression failed: " << failures
                  << " check(s)" << std::endl;
        return 1;
    }

    std::cout << "Any regression: PASS" << std::endl;
    return 0;
}
