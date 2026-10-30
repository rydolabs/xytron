#include "nativeweb/detail/async.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;
void fail(const char* e,const char* f,int l){std::cerr<<f<<":"<<l<<": CHECK failed: "<<e<<std::endl;++failures;}
#define CHECK(e) do{if(!(e))fail(#e,__FILE__,__LINE__);}while(0)

void testArgumentPacking()
{
    VariantList args;
    nativeweb::detail::appendArguments(
        args,
        42,
        std::string("camera"),
        true,
        "literal");

    CHECK(args.size() == 4u);
    CHECK(AnyCast<int>(args[0]) == 42);
    CHECK(AnyCast<std::string>(args[1]) == "camera");
    CHECK(AnyCast<bool>(args[2]));
    CHECK(AnyCast<std::string>(args[3]) == "literal");
}


} // namespace

int main()
{
    testArgumentPacking();

    if(failures){std::cerr<<"Async adapter regression failed: "<<failures<<std::endl;return 1;}
    std::cout<<"Async adapter regression: PASS"<<std::endl;
    return 0;
}
