#ifndef XYTRON_CLI_JSON_VALIDATE_HPP
#define XYTRON_CLI_JSON_VALIDATE_HPP
#include <cctype>
#include <stdexcept>
#include <string>
// Syntax validation only; schema/version/entry contracts are checked separately.
namespace xytron_cli {
class JsonSyntax {
public:
  explicit JsonSyntax(const std::string& text):text_(text),pos_(0){}
  void validate() {
    skip();value(0);skip();
    if(pos_!=text_.size()) invalid();
  }
private:
  void invalid() {throw std::runtime_error("Malformed plugin.json");}
  void skip(){while(pos_<text_.size() && (text_[pos_]==' ' || text_[pos_]=='\n'||text_[pos_]=='\r'||text_[pos_]=='\t')) ++pos_;}
  bool take(char c){skip();if(pos_<text_.size()&&text_[pos_]==c){++pos_;return true;}return false;}
  void required(char c){if(!take(c)) invalid();}
  void word(const char* target){
    for(const char* p=target;*p;++p){
      if(pos_>=text_.size()||text_[pos_]!=*p) invalid(); ++pos_;
    }
  }
  void string() {
    required('"');bool closed=false;
    while(pos_<text_.size()) {
      const unsigned char c=static_cast<unsigned char>(text_[pos_++]);
      if(c=='"'){closed=true;break;}
      if(c<0x20) invalid();
      if(c=='\\'){
        if(pos_>=text_.size())invalid();
        const char esc=text_[pos_++];
        if(esc=='u'){
          for(int k=0;k<4;++k){
            if(pos_>=text_.size()||!std::isxdigit(static_cast<unsigned char>(text_[pos_++])))invalid();
          }
        } else if(esc!='"'&&esc!='\\'&&esc!='/'&&esc!='b'&&esc!='f'&&esc!='n'&&esc!='r'&&esc!='t') invalid();
      }
    }
    if(!closed) invalid();
  }
  void number(){
    if(pos_<text_.size()&&text_[pos_]=='-')++pos_;
    if(pos_>=text_.size())invalid();
    if(text_[pos_]=='0') ++pos_;
    else {
      if(text_[pos_]<'1'||text_[pos_]>'9')invalid();
      while(pos_<text_.size()&&std::isdigit(static_cast<unsigned char>(text_[pos_])))++pos_;
    }
    if(pos_<text_.size()&&text_[pos_]=='.'){
      ++pos_; if(pos_>=text_.size()||!std::isdigit(static_cast<unsigned char>(text_[pos_])))invalid();
      while(pos_<text_.size()&&std::isdigit(static_cast<unsigned char>(text_[pos_])))++pos_;
    }
    if(pos_<text_.size()&&(text_[pos_]=='e'||text_[pos_]=='E')){
      ++pos_;if(pos_<text_.size()&&(text_[pos_]=='+'||text_[pos_]=='-'))++pos_;
      if(pos_>=text_.size()||!std::isdigit(static_cast<unsigned char>(text_[pos_])))invalid();
      while(pos_<text_.size()&&std::isdigit(static_cast<unsigned char>(text_[pos_])))++pos_;
    }
  }
  void value(int depth){
    if(depth>32)invalid();skip();
    if(pos_>=text_.size())invalid();
    if(text_[pos_]=='"')string();
    else if(take('{')){
      if(take('}'))return;
      do {string();required(':');value(depth+1);}while(take(','));
      required('}');
    } else if(take('[')){
      if(take(']'))return;
      do {value(depth+1);}while(take(','));
      required(']');
    } else if(text_[pos_]=='t')word("true");
    else if(text_[pos_]=='f')word("false");
    else if(text_[pos_]=='n')word("null");
    else number();
  }
  const std::string& text_;
  std::size_t pos_;
};
} // namespace xytron_cli
#endif
