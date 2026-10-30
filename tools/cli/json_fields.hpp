#ifndef XYTRON_CLI_JSON_FIELDS_HPP
#define XYTRON_CLI_JSON_FIELDS_HPP
// Schema-v1 manifest fields are resolved structurally, never with regexes.
// JsonSyntax checks the complete document before this object reader runs.
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
namespace xytron_cli {
class JsonObjectFields {
public:
  explicit JsonObjectFields(const std::string& source):source_(source),pos_(0) {
    ws();need('{');ws();
    if(take('}')) {ws();finish();return;}
    while(true) {
      ws();const std::string key=quoted();ws();need(':');ws();
      const std::size_t first=pos_;skipValue();
      if(!fields_.insert(std::make_pair(key,source_.substr(first,pos_-first))).second)
        throw std::runtime_error("Duplicate plugin manifest field: "+key);
      ws();if(take('}'))break;need(',');
    }
    ws();finish();
  }
  bool contains(const std::string& key) const {return fields_.count(key)!=0;}
  std::string raw(const std::string& key) const {
    const std::map<std::string,std::string>::const_iterator it=fields_.find(key);
    if(it==fields_.end())throw std::runtime_error("Missing manifest field: "+key);
    return it->second;
  }
  std::string string(const std::string& key) const {
    const std::string value=raw(key);
    if(value.empty()||value[0]!='"')throw std::runtime_error("Expected string manifest field: "+key);
    std::size_t offset=0;const std::string result=readString(value,offset);
    if(offset!=value.size())throw std::runtime_error("Invalid string manifest field: "+key);
    return result;
  }
  JsonObjectFields object(const std::string& key) const {
    const std::string value=raw(key);
    if(value.empty()||value[0]!='{')throw std::runtime_error("Expected object manifest field: "+key);
    return JsonObjectFields(value);
  }
private:
  static void whitespace(const std::string& s,std::size_t& at) {
    while(at<s.size()&&(s[at]==' '||s[at]=='\r'||s[at]=='\n'||s[at]=='\t'))++at;
  }
  static std::string readString(const std::string& s,std::size_t& at) {
    if(at>=s.size()||s[at++]!='"')throw std::runtime_error("Expected JSON string");
    std::string result;
    while(at<s.size()) {
      const char c=s[at++];
      if(c=='"')return result;
      if(c!='\\'){result+=c;continue;}
      if(at>=s.size())throw std::runtime_error("Malformed JSON escape");
      const char esc=s[at++];
      if(esc=='u') {
        if(at+4>s.size())throw std::runtime_error("Malformed Unicode escape");
        unsigned n=0;
        for(int k=0;k<4;++k){
          const char h=s[at++];
          const unsigned d=(h>='0'&&h<='9')?h-'0':
            (h>='A'&&h<='F')?h-'A'+10:(h>='a'&&h<='f')?h-'a'+10:999;
          if(d==999)throw std::runtime_error("Malformed Unicode escape");
          n=n*16+d;
        }
        // The v1 manifest contract only uses ASCII field names/values.
        // Reject non-ASCII escapes rather than mis-normalize identifiers.
        if(n>0x7f || n<0x20)throw std::runtime_error("Unsupported Unicode manifest escape");
        result+=static_cast<char>(n);
      } else {
        switch(esc){
          case '"':result+='"';break;case '\\':result+='\\';break;
          case '/':result+='/';break;case 'b':result+='\b';break;
          case 'f':result+='\f';break;case 'n':result+='\n';break;
          case 'r':result+='\r';break;case 't':result+='\t';break;
          default:throw std::runtime_error("Malformed JSON escape");
        }
      }
    }
    throw std::runtime_error("Unclosed JSON string");
  }
  void ws(){whitespace(source_,pos_);}
  bool take(char c){if(pos_<source_.size()&&source_[pos_]==c){++pos_;return true;}return false;}
  void need(char c){if(!take(c))throw std::runtime_error("Invalid manifest object structure");}
  std::string quoted(){return readString(source_,pos_);}
  void skipValue() {
    if(pos_>=source_.size())throw std::runtime_error("Incomplete manifest value");
    if(source_[pos_]=='"'){quoted();return;}
    if(source_[pos_]=='{'||source_[pos_]=='['){
      const char opening=source_[pos_++];
      const char closing=opening=='{'?'}':']';
      ws();if(take(closing))return;
      while(true){
        if(opening=='{'){ws();quoted();ws();need(':');ws();}
        skipValue();ws();if(take(closing))break;need(',');ws();
      }
      return;
    }
    const std::size_t start=pos_;
    while(pos_<source_.size()&&source_[pos_]!=','&&source_[pos_]!='}'&&
          source_[pos_]!=']'&&!std::isspace(static_cast<unsigned char>(source_[pos_])))++pos_;
    if(start==pos_)throw std::runtime_error("Invalid manifest scalar");
  }
  void finish(){if(pos_!=source_.size())throw std::runtime_error("Trailing manifest data");}
  const std::string source_;
  std::size_t pos_;
  std::map<std::string,std::string> fields_;
};
} // namespace xytron_cli
#endif
