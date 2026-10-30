#include "core/json_codec.hpp"

#include "nativeweb/types.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace nativeweb {
namespace detail {

namespace {

void appendEscapedString(
    std::ostringstream& output,
    const std::string& value)
{
    output << '"';

    for (std::size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char ch =
            static_cast<unsigned char>(value[i]);

        switch (ch)
        {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                output
                    << "\\u"
                    << std::hex
                    << std::setw(4)
                    << std::setfill('0')
                    << static_cast<unsigned int>(ch)
                    << std::dec
                    << std::setfill(' ');
            }
            else
            {
                output << static_cast<char>(ch);
            }
            break;
        }
    }

    output << '"';
}

const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void appendBase64(std::ostringstream& output, const Binary& data)
{
    for (std::size_t i=0;i<data.size();i+=3)
    {
        const unsigned int a=data[i];
        const unsigned int b=i+1<data.size()?data[i+1]:0;
        const unsigned int c=i+2<data.size()?data[i+2]:0;
        output << kBase64Alphabet[a>>2]
               << kBase64Alphabet[((a&3)<<4)|(b>>4)]
               << (i+1<data.size()?kBase64Alphabet[((b&15)<<2)|(c>>6)]:'=')
               << (i+2<data.size()?kBase64Alphabet[c&63]:'=');
    }
}

int base64Digit(char c)
{
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    return -1;
}

Binary decodeBase64(const std::string& encoded)
{
    if(encoded.size()%4!=0)
        throw std::runtime_error("Invalid NativeWeb base64 length");
    Binary bytes;
    bytes.reserve(encoded.size()/4*3);
    for(std::size_t i=0;i<encoded.size();i+=4)
    {
        const int a=base64Digit(encoded[i]);
        const int b=base64Digit(encoded[i+1]);
        const bool last=i+4==encoded.size();
        const bool twoPad=encoded[i+2]=='=';
        const bool onePad=encoded[i+3]=='=';
        const int c=twoPad?0:base64Digit(encoded[i+2]);
        const int d=onePad?0:base64Digit(encoded[i+3]);
        if(a<0||b<0||c<0||d<0||
           (twoPad&&!onePad)||((twoPad||onePad)&&!last)||
           (twoPad&&(b&15)!=0)||(onePad&&!twoPad&&(c&3)!=0))
            throw std::runtime_error("Invalid NativeWeb base64 bytes");
        bytes.push_back(static_cast<unsigned char>((a<<2)|(b>>4)));
        if(!twoPad)
            bytes.push_back(static_cast<unsigned char>(((b&15)<<4)|(c>>2)));
        if(!onePad)
            bytes.push_back(static_cast<unsigned char>(((c&3)<<6)|d));
    }
    return bytes;
}

void encodeJson(
    std::ostringstream& output,
    const Any& value, bool compactBinary);

void encodeList(
    std::ostringstream& output,
    const VariantList& list, bool compactBinary)
{
    output << '[';

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (i != 0)
            output << ',';

        encodeJson(output, list[i], compactBinary);
    }

    output << ']';
}

void encodeDictionary(
    std::ostringstream& output,
    const VariantDict& dict, bool compactBinary)
{
    output << '{';

    bool first = true;

    for (VariantDict::const_iterator it = dict.begin();
         it != dict.end();
         ++it)
    {
        if (!first)
            output << ',';

        first = false;

        appendEscapedString(output, it->first);
        output << ':';
        encodeJson(output, it->second, compactBinary);
    }

    output << '}';
}

void encodeJson(
    std::ostringstream& output,
    const Any& value, bool compactBinary)
{
    if (value.empty())
    {
        output << "null";
        return;
    }

    if (value.type() == typeid(bool))
    {
        output <<
            (AnyCast<bool>(value)
                ? "true"
                : "false");
        return;
    }

    if (value.type() == typeid(int))
    {
        output << AnyCast<int>(value);
        return;
    }

    if (value.type() == typeid(double))
    {
        const double number =
            AnyCast<double>(value);

        if (!std::isfinite(number))
            throw std::runtime_error(
                "NativeWeb JSON does not support non-finite numbers");

        output << std::setprecision(17) << number;
        return;
    }

    if (value.type() == typeid(std::string))
    {
        appendEscapedString(
            output,
            AnyCast<std::string>(value));
        return;
    }

    if (value.type() == typeid(VariantList))
    {
        encodeList(
            output,
            AnyCast<const VariantList&>(value), compactBinary);
        return;
    }

    if (value.type() == typeid(VariantDict))
    {
        encodeDictionary(
            output,
            AnyCast<const VariantDict&>(value), compactBinary);
        return;
    }

    if (value.type() == typeid(Binary))
    {
        const Binary& bytes =
            AnyCast<const Binary&>(value);

        if (compactBinary)
        {
            output << "{\"__nativeweb_binary_base64\":\"";
            appendBase64(output, bytes);
            output << "\"}";
            return;
        }

        output <<
            "{\"__nativeweb_binary\":[";

        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            if (i != 0)
                output << ',';

            output <<
                static_cast<unsigned int>(bytes[i]);
        }

        output << "]}";
        return;
    }

    if (value.type() == typeid(NativeObjectHandle))
    {
        const NativeObjectHandle& handle =
            AnyCast<const NativeObjectHandle&>(value);

        output <<
            "{\"__nativeweb_object\":true,"
            "\"id\":";

        std::ostringstream id;
        id << handle.id;

        appendEscapedString(
            output,
            id.str());

        output << ",\"type\":";
        appendEscapedString(
            output,
            handle.type);

        output << '}';
        return;
    }

    throw std::runtime_error(
        "Unsupported Any type for NativeWeb JSON: " +
        std::string(value.type().name()));
}

void appendUtf8(
    std::string& output,
    std::uint32_t codepoint)
{
    if (codepoint <= 0x7f)
    {
        output.push_back(
            static_cast<char>(codepoint));
        return;
    }

    if (codepoint <= 0x7ff)
    {
        output.push_back(
            static_cast<char>(
                0xc0 | (codepoint >> 6)));

        output.push_back(
            static_cast<char>(
                0x80 | (codepoint & 0x3f)));

        return;
    }

    if (codepoint <= 0xffff)
    {
        output.push_back(
            static_cast<char>(
                0xe0 | (codepoint >> 12)));

        output.push_back(
            static_cast<char>(
                0x80 |
                ((codepoint >> 6) & 0x3f)));

        output.push_back(
            static_cast<char>(
                0x80 | (codepoint & 0x3f)));

        return;
    }

    if (codepoint <= 0x10ffff)
    {
        output.push_back(
            static_cast<char>(
                0xf0 | (codepoint >> 18)));

        output.push_back(
            static_cast<char>(
                0x80 |
                ((codepoint >> 12) & 0x3f)));

        output.push_back(
            static_cast<char>(
                0x80 |
                ((codepoint >> 6) & 0x3f)));

        output.push_back(
            static_cast<char>(
                0x80 | (codepoint & 0x3f)));

        return;
    }

    throw std::runtime_error(
        "Invalid Unicode code point in NativeWeb JSON");
}

class Parser
{
public:
    explicit Parser(const std::string& json)
        : json_(json),
          position_(0)
    {
    }

    Any parse()
    {
        skipWhitespace();

        Any value = parseValue();

        skipWhitespace();

        if (position_ != json_.size())
            fail("Trailing data");

        return value;
    }

private:
    void skipWhitespace()
    {
        while (position_ < json_.size())
        {
            const char ch =
                json_[position_];

            if (ch != ' ' &&
                ch != '\t' &&
                ch != '\r' &&
                ch != '\n')
            {
                break;
            }

            ++position_;
        }
    }

    bool consume(char expected)
    {
        if (position_ >= json_.size() ||
            json_[position_] != expected)
        {
            return false;
        }

        ++position_;
        return true;
    }

    void require(char expected)
    {
        if (!consume(expected))
        {
            std::string message =
                "Expected '";
            message.push_back(expected);
            message += "'";
            fail(message);
        }
    }

    void fail(const std::string& message) const
    {
        std::ostringstream stream;
        stream
            << "Invalid NativeWeb JSON at "
            << position_
            << ": "
            << message;

        throw std::runtime_error(
            stream.str());
    }

    Any parseValue()
    {
        skipWhitespace();

        if (position_ >= json_.size())
            fail("Unexpected end of input");

        const char ch =
            json_[position_];

        if (ch == '"')
            return Any(parseString());

        if (ch == '{')
            return parseObject();

        if (ch == '[')
            return parseArray();

        if (ch == 't')
        {
            parseLiteral("true");
            return Any(true);
        }

        if (ch == 'f')
        {
            parseLiteral("false");
            return Any(false);
        }

        if (ch == 'n')
        {
            parseLiteral("null");
            return Any();
        }

        if (ch == '-' ||
            (ch >= '0' && ch <= '9'))
        {
            return parseNumber();
        }

        fail("Unexpected token");
        return Any();
    }

    void parseLiteral(
        const char* literal)
    {
        const std::size_t start =
            position_;

        while (*literal)
        {
            if (position_ >= json_.size() ||
                json_[position_] != *literal)
            {
                position_ = start;
                fail("Invalid literal");
            }

            ++position_;
            ++literal;
        }
    }

    static int hexValue(char ch)
    {
        if (ch >= '0' && ch <= '9')
            return ch - '0';

        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;

        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;

        return -1;
    }

    std::uint32_t parseHex4()
    {
        if (position_ + 4 > json_.size())
            fail("Incomplete Unicode escape");

        std::uint32_t value = 0;

        for (int i = 0; i < 4; ++i)
        {
            const int digit =
                hexValue(json_[position_++]);

            if (digit < 0)
                fail("Invalid Unicode escape");

            value =
                (value << 4) |
                static_cast<std::uint32_t>(digit);
        }

        return value;
    }

    std::string parseString()
    {
        require('"');

        std::string output;

        while (position_ < json_.size())
        {
            const char ch =
                json_[position_++];

            if (ch == '"')
                return output;

            if (ch == '\\')
            {
                if (position_ >= json_.size())
                    fail("Incomplete escape");

                const char escaped =
                    json_[position_++];

                switch (escaped)
                {
                case '"':
                    output.push_back('"');
                    break;
                case '\\':
                    output.push_back('\\');
                    break;
                case '/':
                    output.push_back('/');
                    break;
                case 'b':
                    output.push_back('\b');
                    break;
                case 'f':
                    output.push_back('\f');
                    break;
                case 'n':
                    output.push_back('\n');
                    break;
                case 'r':
                    output.push_back('\r');
                    break;
                case 't':
                    output.push_back('\t');
                    break;
                case 'u':
                {
                    std::uint32_t codepoint =
                        parseHex4();

                    if (codepoint >= 0xd800 &&
                        codepoint <= 0xdbff)
                    {
                        if (position_ + 2 >
                                json_.size() ||
                            json_[position_] != '\\' ||
                            json_[position_ + 1] != 'u')
                        {
                            fail(
                                "Missing low surrogate");
                        }

                        position_ += 2;

                        const std::uint32_t low =
                            parseHex4();

                        if (low < 0xdc00 ||
                            low > 0xdfff)
                        {
                            fail(
                                "Invalid low surrogate");
                        }

                        codepoint =
                            0x10000 +
                            ((codepoint - 0xd800) << 10) +
                            (low - 0xdc00);
                    }
                    else if (
                        codepoint >= 0xdc00 &&
                        codepoint <= 0xdfff)
                    {
                        fail(
                            "Unexpected low surrogate");
                    }

                    appendUtf8(
                        output,
                        codepoint);
                    break;
                }
                default:
                    fail("Invalid string escape");
                }

                continue;
            }

            if (static_cast<unsigned char>(ch) < 0x20)
                fail("Unescaped control character");

            output.push_back(ch);
        }

        fail("Unterminated string");
        return std::string();
    }

    Any parseArray()
    {
        require('[');
        skipWhitespace();

        VariantList list;

        if (consume(']'))
            return Any(list);

        for (;;)
        {
            list.push_back(
                parseValue());

            skipWhitespace();

            if (consume(']'))
                break;

            require(',');
            skipWhitespace();
        }

        return Any(list);
    }

    Any parseObject()
    {
        require('{');
        skipWhitespace();

        VariantDict dict;

        if (consume('}'))
            return Any(dict);

        for (;;)
        {
            skipWhitespace();

            if (position_ >= json_.size() ||
                json_[position_] != '"')
            {
                fail("Object key must be a string");
            }

            const std::string key =
                parseString();

            skipWhitespace();
            require(':');
            skipWhitespace();

            if (dict.find(key) != dict.end())
                fail("Duplicate JSON object key");

            dict[key] = parseValue();

            skipWhitespace();

            if (consume('}'))
                break;

            require(',');
            skipWhitespace();
        }

        VariantDict::const_iterator compactIt =
            dict.find("__nativeweb_binary_base64");
        if(compactIt!=dict.end())
        {
            if(dict.size()!=1 || compactIt->second.type()!=typeid(std::string))
                fail("Invalid NativeWeb compact binary marker");
            try {
                return Any(decodeBase64(AnyCast<std::string>(compactIt->second)));
            } catch(const std::runtime_error&) {
                fail("Invalid NativeWeb base64 binary");
            }
        }

        VariantDict::const_iterator binaryIt =
            dict.find("__nativeweb_binary");

        if (binaryIt != dict.end())
        {
            if (dict.size() != 1 ||
                binaryIt->second.type() !=
                    typeid(VariantList))
            {
                fail(
                    "Invalid NativeWeb binary marker");
            }

            const VariantList& values =
                AnyCast<const VariantList&>(
                    binaryIt->second);

            Binary bytes;
            bytes.reserve(values.size());

            for (std::size_t i = 0;
                 i < values.size();
                 ++i)
            {
                if (values[i].type() != typeid(int))
                    fail(
                        "Binary byte is not an integer");

                const int byte =
                    AnyCast<int>(values[i]);

                if (byte < 0 || byte > 255)
                    fail(
                        "Binary byte is out of range");

                bytes.push_back(
                    static_cast<unsigned char>(byte));
            }

            return Any(bytes);
        }

        VariantDict::const_iterator objectMarker =
            dict.find("__nativeweb_object");

        if (objectMarker != dict.end() &&
            objectMarker->second.type() == typeid(bool) &&
            AnyCast<bool>(objectMarker->second))
        {
            VariantDict::const_iterator idIt =
                dict.find("id");

            VariantDict::const_iterator typeIt =
                dict.find("type");

            if (idIt == dict.end() ||
                typeIt == dict.end())
            {
                fail(
                    "Incomplete NativeWeb object marker");
            }

            const std::string idText =
                AnyCast<std::string>(idIt->second);

            std::istringstream stream(idText);
            std::uint64_t id = 0;
            stream >> id;

            if (!stream ||
                !stream.eof() ||
                id == 0)
            {
                fail(
                    "Invalid NativeWeb object id");
            }

            return Any(
                NativeObjectHandle(
                    id,
                    AnyCast<std::string>(
                        typeIt->second)));
        }

        return Any(dict);
    }

    Any parseNumber()
    {
        const std::size_t start =
            position_;

        if (consume('-'))
        {
        }

        if (position_ >= json_.size())
            fail("Incomplete number");

        if (json_[position_] == '0')
        {
            ++position_;
        }
        else
        {
            if (!std::isdigit(
                    static_cast<unsigned char>(
                        json_[position_])))
            {
                fail("Invalid number");
            }

            while (
                position_ < json_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(
                        json_[position_])))
            {
                ++position_;
            }
        }

        bool floating = false;

        if (position_ < json_.size() &&
            json_[position_] == '.')
        {
            floating = true;
            ++position_;

            if (position_ >= json_.size() ||
                !std::isdigit(
                    static_cast<unsigned char>(
                        json_[position_])))
            {
                fail("Invalid fraction");
            }

            while (
                position_ < json_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(
                        json_[position_])))
            {
                ++position_;
            }
        }

        if (position_ < json_.size() &&
            (json_[position_] == 'e' ||
             json_[position_] == 'E'))
        {
            floating = true;
            ++position_;

            if (position_ < json_.size() &&
                (json_[position_] == '+' ||
                 json_[position_] == '-'))
            {
                ++position_;
            }

            if (position_ >= json_.size() ||
                !std::isdigit(
                    static_cast<unsigned char>(
                        json_[position_])))
            {
                fail("Invalid exponent");
            }

            while (
                position_ < json_.size() &&
                std::isdigit(
                    static_cast<unsigned char>(
                        json_[position_])))
            {
                ++position_;
            }
        }

        const std::string text =
            json_.substr(
                start,
                position_ - start);

        if (!floating)
        {
            char* end = 0;
            const long value =
                std::strtol(
                    text.c_str(),
                    &end,
                    10);

            if (end &&
                *end == '\0' &&
                value >=
                    std::numeric_limits<int>::min() &&
                value <=
                    std::numeric_limits<int>::max())
            {
                return Any(
                    static_cast<int>(value));
            }
        }

        char* end = 0;
        const double value =
            std::strtod(
                text.c_str(),
                &end);

        if (!end ||
            *end != '\0' ||
            !std::isfinite(value))
        {
            fail("Invalid numeric value");
        }

        return Any(value);
    }

    const std::string& json_;
    std::size_t position_;
};

} // namespace

std::string anyToJson(const Any& value)
{
    std::ostringstream output;
    encodeJson(output, value, false);
    return output.str();
}

std::string anyToJsonCompactBinary(const Any& value)
{
    std::ostringstream output;
    encodeJson(output, value, true);
    return output.str();
}

Any jsonToAny(const std::string& json)
{
    Parser parser(json);
    return parser.parse();
}

} // namespace detail
} // namespace nativeweb
