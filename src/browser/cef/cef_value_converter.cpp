#include "browser/cef/cef_value_converter.hpp"

#include "nativeweb/types.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace nativeweb {
namespace detail {

namespace {

Any cefListToAny(CefRefPtr<CefListValue> list)
{
    VariantList output;
    const std::size_t size = list ? list->GetSize() : 0;
    output.reserve(size);

    for (std::size_t i = 0; i < size; ++i)
        output.push_back(cefValueToAny(list->GetValue(i)));

    return Any(output);
}

Any cefDictionaryToAny(CefRefPtr<CefDictionaryValue> dict)
{
    if (dict &&
        dict->HasKey("__nativeweb_object") &&
        dict->GetBool("__nativeweb_object"))
    {
        const std::string idText =
            dict->GetString("id").ToString();

        std::istringstream stream(idText);
        std::uint64_t id = 0;
        stream >> id;

        if (!stream || !stream.eof() || id == 0)
            throw std::runtime_error("Invalid NativeWeb object id");

        return Any(
            NativeObjectHandle(
                id,
                dict->GetString("type").ToString()));
    }

    VariantDict output;

    CefDictionaryValue::KeyList keys;
    if (dict)
        dict->GetKeys(keys);

    for (CefDictionaryValue::KeyList::const_iterator it = keys.begin();
         it != keys.end();
         ++it)
    {
        output[it->ToString()] =
            cefValueToAny(dict->GetValue(*it));
    }

    return Any(output);
}

CefRefPtr<CefListValue> anyListToCef(const VariantList& list)
{
    CefRefPtr<CefListValue> output = CefListValue::Create();
    output->SetSize(list.size());

    for (std::size_t i = 0; i < list.size(); ++i)
        output->SetValue(i, anyToCefValue(list[i]));

    return output;
}

CefRefPtr<CefDictionaryValue> anyDictionaryToCef(const VariantDict& dict)
{
    CefRefPtr<CefDictionaryValue> output =
        CefDictionaryValue::Create();

    for (VariantDict::const_iterator it = dict.begin();
         it != dict.end();
         ++it)
    {
        output->SetValue(it->first, anyToCefValue(it->second));
    }

    return output;
}

} // namespace

Any cefValueToAny(CefRefPtr<CefValue> value)
{
    if (!value || !value->IsValid())
        return Any();

    switch (value->GetType())
    {
    case VTYPE_INVALID:
    case VTYPE_NULL:
        return Any();

    case VTYPE_BOOL:
        return Any(value->GetBool());

    case VTYPE_INT:
        return Any(value->GetInt());

    case VTYPE_DOUBLE:
        return Any(value->GetDouble());

    case VTYPE_STRING:
        return Any(value->GetString().ToString());

    case VTYPE_BINARY:
    {
        CefRefPtr<CefBinaryValue> binary = value->GetBinary();
        const std::size_t size = binary ? binary->GetSize() : 0;
        Binary bytes(size);

        if (size > 0)
            binary->GetData(&bytes[0], size, 0);

        return Any(bytes);
    }

    case VTYPE_DICTIONARY:
        return cefDictionaryToAny(value->GetDictionary());

    case VTYPE_LIST:
        return cefListToAny(value->GetList());

    case VTYPE_NUM_VALUES:
        break;
    }

    throw std::runtime_error("Unsupported CEF value type");
}

CefRefPtr<CefValue> anyToCefValue(const Any& value)
{
    CefRefPtr<CefValue> output = CefValue::Create();

    if (value.empty())
    {
        output->SetNull();
        return output;
    }

    if (value.type() == typeid(bool))
    {
        output->SetBool(AnyCast<bool>(value));
        return output;
    }

    if (value.type() == typeid(int))
    {
        output->SetInt(AnyCast<int>(value));
        return output;
    }

    if (value.type() == typeid(double))
    {
        output->SetDouble(AnyCast<double>(value));
        return output;
    }

    if (value.type() == typeid(std::string))
    {
        output->SetString(AnyCast<std::string>(value));
        return output;
    }

    if (value.type() == typeid(VariantList))
    {
        output->SetList(
            anyListToCef(AnyCast<const VariantList&>(value)));
        return output;
    }

    if (value.type() == typeid(VariantDict))
    {
        output->SetDictionary(
            anyDictionaryToCef(AnyCast<const VariantDict&>(value)));
        return output;
    }

    if (value.type() == typeid(NativeObjectHandle))
    {
        const NativeObjectHandle& handle =
            AnyCast<const NativeObjectHandle&>(value);

        CefRefPtr<CefDictionaryValue> dict =
            CefDictionaryValue::Create();

        std::ostringstream id;
        id << handle.id;

        dict->SetBool("__nativeweb_object", true);
        dict->SetString("id", id.str());
        dict->SetString("type", handle.type);

        output->SetDictionary(dict);
        return output;
    }

    if (value.type() == typeid(Binary))
    {
        const Binary& bytes = AnyCast<const Binary&>(value);
        CefRefPtr<CefBinaryValue> binary =
            CefBinaryValue::Create(
                bytes.empty() ? 0 : &bytes[0],
                bytes.size());

        output->SetBinary(binary);
        return output;
    }

    throw std::runtime_error(
        "Unsupported Any type for CEF transport: " +
        std::string(value.type().name()));
}

CefRefPtr<CefValue> v8ToCefValue(CefRefPtr<CefV8Value> value)
{
    CefRefPtr<CefValue> output = CefValue::Create();

    if (!value || value->IsUndefined() || value->IsNull())
    {
        output->SetNull();
        return output;
    }

    if (value->IsBool())
    {
        output->SetBool(value->GetBoolValue());
        return output;
    }

    if (value->IsInt())
    {
        output->SetInt(value->GetIntValue());
        return output;
    }

    if (value->IsUInt())
    {
        output->SetDouble(
            static_cast<double>(value->GetUIntValue()));
        return output;
    }

    if (value->IsDouble())
    {
        output->SetDouble(value->GetDoubleValue());
        return output;
    }

    if (value->IsString())
    {
        output->SetString(value->GetStringValue());
        return output;
    }

    if (value->IsArray())
    {
        const int length = value->GetArrayLength();
        CefRefPtr<CefListValue> list = CefListValue::Create();
        list->SetSize(length);

        for (int i = 0; i < length; ++i)
            list->SetValue(i, v8ToCefValue(value->GetValue(i)));

        output->SetList(list);
        return output;
    }

    if (value->IsArrayBuffer())
    {
        const std::size_t size = value->GetArrayBufferByteLength();
        void* data = value->GetArrayBufferData();
        output->SetBinary(CefBinaryValue::Create(data, size));
        return output;
    }

    if (value->IsObject())
    {
        CefRefPtr<CefDictionaryValue> dict =
            CefDictionaryValue::Create();

        std::vector<CefString> keys;
        value->GetKeys(keys);

        for (std::size_t i = 0; i < keys.size(); ++i)
        {
            dict->SetValue(
                keys[i],
                v8ToCefValue(value->GetValue(keys[i])));
        }

        output->SetDictionary(dict);
        return output;
    }

    throw std::runtime_error(
        "Unsupported JavaScript value for NativeWeb bridge");
}

CefRefPtr<CefV8Value> cefValueToV8(CefRefPtr<CefValue> value)
{
    if (!value || !value->IsValid())
        return CefV8Value::CreateNull();

    switch (value->GetType())
    {
    case VTYPE_INVALID:
    case VTYPE_NULL:
        return CefV8Value::CreateNull();

    case VTYPE_BOOL:
        return CefV8Value::CreateBool(value->GetBool());

    case VTYPE_INT:
        return CefV8Value::CreateInt(value->GetInt());

    case VTYPE_DOUBLE:
        return CefV8Value::CreateDouble(value->GetDouble());

    case VTYPE_STRING:
        return CefV8Value::CreateString(value->GetString());

    case VTYPE_BINARY:
    {
        CefRefPtr<CefBinaryValue> binary = value->GetBinary();
        const std::size_t size = binary ? binary->GetSize() : 0;
        std::vector<unsigned char> bytes(size);

        if (size > 0)
            binary->GetData(&bytes[0], size, 0);

        return CefV8Value::CreateArrayBufferWithCopy(
            bytes.empty() ? 0 : &bytes[0],
            bytes.size());
    }

    case VTYPE_LIST:
    {
        CefRefPtr<CefListValue> list = value->GetList();
        const std::size_t size = list ? list->GetSize() : 0;
        CefRefPtr<CefV8Value> array =
            CefV8Value::CreateArray(static_cast<int>(size));

        for (std::size_t i = 0; i < size; ++i)
            array->SetValue(
                static_cast<int>(i),
                cefValueToV8(list->GetValue(i)));

        return array;
    }

    case VTYPE_DICTIONARY:
    {
        CefRefPtr<CefDictionaryValue> dict =
            value->GetDictionary();
        CefRefPtr<CefV8Value> object =
            CefV8Value::CreateObject(nullptr, nullptr);

        CefDictionaryValue::KeyList keys;
        if (dict)
            dict->GetKeys(keys);

        for (CefDictionaryValue::KeyList::const_iterator it =
                 keys.begin();
             it != keys.end();
             ++it)
        {
            object->SetValue(
                *it,
                cefValueToV8(dict->GetValue(*it)),
                V8_PROPERTY_ATTRIBUTE_NONE);
        }

        return object;
    }

    case VTYPE_NUM_VALUES:
        break;
    }

    return CefV8Value::CreateNull();
}

} // namespace detail
} // namespace nativeweb
