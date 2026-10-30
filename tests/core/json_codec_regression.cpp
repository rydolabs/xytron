#include "core/json_codec.hpp"

#include "nativeweb/types.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void fail(
    const char* expression,
    const char* file,
    int line)
{
    std::cerr
        << file << ":" << line
        << ": CHECK failed: "
        << expression
        << std::endl;

    ++failures;
}

#define CHECK(expr) \
    do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

void testStructuredRoundTrip()
{
    VariantList values;
    values.push_back(Any(1));
    values.push_back(Any("two"));
    values.push_back(Any(true));

    VariantDict nested;
    nested["count"] = 2;

    VariantDict root;
    root["name"] = "camera";
    root["values"] = values;
    root["nested"] = nested;

    const std::string json =
        nativeweb::detail::anyToJson(
            Any(root));

    const Any decodedValue =
        nativeweb::detail::jsonToAny(json);

    const VariantDict& decoded =
        AnyCast<const VariantDict&>(
            decodedValue);

    CHECK(
        AnyCast<std::string>(
            decoded.find("name")->second) ==
        "camera");

    const VariantList& decodedValues =
        AnyCast<const VariantList&>(
            decoded.find("values")->second);

    CHECK(decodedValues.size() == 3u);
    CHECK(AnyCast<int>(decodedValues[0]) == 1);
    CHECK(
        AnyCast<std::string>(
            decodedValues[1]) ==
        "two");
    CHECK(AnyCast<bool>(decodedValues[2]));
}

void testUnicodeAndEscapes()
{
    const Any decoded =
        nativeweb::detail::jsonToAny(
            "\"hello\\n\\uD55C\\uAE00\"");

    CHECK(
        AnyCast<std::string>(decoded) ==
        std::string("hello\n") +
        "\xED\x95\x9C\xEA\xB8\x80");

    const std::string encoded =
        nativeweb::detail::anyToJson(
            Any(std::string("a\nb")));

    CHECK(encoded == "\"a\\nb\"");
}

void testBinaryRoundTrip()
{
    nativeweb::Binary bytes;
    bytes.push_back(0);
    bytes.push_back(127);
    bytes.push_back(255);

    const Any decoded =
        nativeweb::detail::jsonToAny(
            nativeweb::detail::anyToJson(
                Any(bytes)));

    const nativeweb::Binary& result =
        AnyCast<const nativeweb::Binary&>(
            decoded);

    CHECK(result.size() == 3u);
    CHECK(result[0] == 0);
    CHECK(result[1] == 127);
    CHECK(result[2] == 255);
}

void testCompactBinaryRoundTrip()
{
    const std::string marker="__nativeweb_binary_base64";
    const nativeweb::Binary sample{0,127,255};
    const std::string compact=nativeweb::detail::anyToJsonCompactBinary(Any(sample));
    CHECK(compact == "{\"__nativeweb_binary_base64\":\"AH//\"}");
    CHECK(nativeweb::detail::anyToJson(Any(sample)) ==
          "{\"__nativeweb_binary\":[0,127,255]}");
    const nativeweb::Binary decoded =
        AnyCast<nativeweb::Binary>(nativeweb::detail::jsonToAny(compact));
    CHECK(decoded==sample);
    for (std::size_t length=0;length<=5;length++)
    {
        nativeweb::Binary bytes(length);
        for(std::size_t i=0;i<length;i++) bytes[i]=static_cast<unsigned char>(i*37);
        const nativeweb::Binary restored=AnyCast<nativeweb::Binary>(
            nativeweb::detail::jsonToAny(
                nativeweb::detail::anyToJsonCompactBinary(Any(bytes))));
        CHECK(bytes==restored);
    }
    nativeweb::Binary large(65536);
    for(std::size_t i=0;i<large.size();++i)
        large[i]=static_cast<unsigned char>((i*113)%256);
    VariantDict dictionary;
    VariantList nested;
    nested.push_back(Any(large));
    nested.push_back(Any(nativeweb::Binary()));
    dictionary["nested"]=nested;
    const std::string encoded=nativeweb::detail::anyToJsonCompactBinary(Any(dictionary));
    CHECK(encoded.find(marker)!=std::string::npos);
    CHECK(encoded.find("__nativeweb_binary\":")==std::string::npos);
    const Any decodedObject=nativeweb::detail::jsonToAny(encoded);
    const VariantDict& parsed=AnyCast<const VariantDict&>(decodedObject);
    const VariantList& values=AnyCast<const VariantList&>(
        parsed.find("nested")->second);
    CHECK(AnyCast<nativeweb::Binary>(values[0])==large);
    CHECK(AnyCast<nativeweb::Binary>(values[1]).empty());
}

void testInvalidCompactBinary()
{
    const char* invalid[]={
        "{\"__nativeweb_binary_base64\":\"A\"}",
        "{\"__nativeweb_binary_base64\":\"*AAA\"}",
        "{\"__nativeweb_binary_base64\":\"AA=A\"}",
        "{\"__nativeweb_binary_base64\":\"AB==\"}",
        "{\"__nativeweb_binary_base64\":\"AAA=AAAA\"}",
        "{\"__nativeweb_binary_base64\":\"AA==\",\"extra\":1}",
        "{\"__nativeweb_binary_base64\":[1,2]}"
    };
    for(std::size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
    {
        bool rejected=false;
        try { (void)nativeweb::detail::jsonToAny(invalid[i]); }
        catch(const std::runtime_error&) { rejected=true; }
        CHECK(rejected);
    }
}

void testObjectHandleRoundTrip()
{
    const nativeweb::NativeObjectHandle input(
        42,
        "Camera");

    const Any decoded =
        nativeweb::detail::jsonToAny(
            nativeweb::detail::anyToJson(
                Any(input)));

    const nativeweb::NativeObjectHandle& result =
        AnyCast<const nativeweb::NativeObjectHandle&>(
            decoded);

    CHECK(result.id == 42u);
    CHECK(result.type == "Camera");
}

void testNumbers()
{
    CHECK(
        AnyCast<int>(
            nativeweb::detail::jsonToAny("42")) ==
        42);

    CHECK(
        AnyCast<double>(
            nativeweb::detail::jsonToAny("3.5")) ==
        3.5);
}

void testInvalidJson()
{
    bool threw = false;

    try
    {
        (void)nativeweb::detail::jsonToAny(
            "{\"a\":]");
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    CHECK(threw);
}

} // namespace

int main()
{
    testStructuredRoundTrip();
    testUnicodeAndEscapes();
    testBinaryRoundTrip();
    testCompactBinaryRoundTrip();
    testInvalidCompactBinary();
    testObjectHandleRoundTrip();
    testNumbers();
    testInvalidJson();

    if (failures != 0)
    {
        std::cerr
            << "JSON codec regression failed: "
            << failures
            << " check(s)"
            << std::endl;

        return 1;
    }

    std::cout
        << "JSON codec regression: PASS"
        << std::endl;

    return 0;
}
