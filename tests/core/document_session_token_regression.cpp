#include "browser/common/document_session_token.hpp"

#include <iostream>
#include <set>
#include <string>

int main()
{
    std::set<std::string> seen;
    std::string previous;
    for (int i = 0; i < 64; ++i) {
        const std::string token=nativeweb::detail::createDocumentSessionToken();
        if (token.size()!=48 || !seen.insert(token).second) return 1;
        for (std::size_t j=0; j<token.size(); ++j)
            if (!(token[j]>='0' && token[j]<='9') &&
                !(token[j]>='a' && token[j]<='f')) return 2;
        if (!nativeweb::detail::matchesDocumentSessionToken(token,token))
            return 3;
        if (!previous.empty() &&
            nativeweb::detail::matchesDocumentSessionToken(previous,token))
            return 4;
        if (nativeweb::detail::matchesDocumentSessionToken("",token))
            return 5;
        if (nativeweb::detail::matchesDocumentSessionToken(token.substr(1),token))
            return 6;
        std::string tampered=token;
        tampered[5]=tampered[5]=='a'?'b':'a';
        if (nativeweb::detail::matchesDocumentSessionToken(tampered,token))
            return 7;
        previous=token;
    }
    std::cout<<"Document session token rotation and exact-match: PASS\n";
    return 0;
}
