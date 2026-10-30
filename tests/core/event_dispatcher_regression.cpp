#include "core/event_dispatcher.hpp"

#include <iostream>
#include <string>
#include <stdexcept>

namespace {

int failures=0;
void fail(const char* e,const char* f,int l){std::cerr<<f<<":"<<l<<": CHECK failed: "<<e<<std::endl;++failures;}
#define CHECK(e) do{if(!(e))fail(#e,__FILE__,__LINE__);}while(0)

void testSubscribeEmitUnsubscribe()
{
    nativeweb::detail::EventDispatcher events;
    int first=0;
    int second=0;

    const nativeweb::detail::EventSubscriptionId a =
        events.subscribe("frame",[&first](const Any& value){
            first += AnyCast<int>(value);
        });

    const nativeweb::detail::EventSubscriptionId b =
        events.subscribe("frame",[&second](const Any& value){
            second += AnyCast<int>(value);
        });

    events.subscribe("other",[](const Any&){});

    CHECK(a != 0);
    CHECK(b != 0);
    CHECK(a != b);
    CHECK(events.size() == 3u);

    CHECK(events.emit("frame",Any(3)) == 2u);
    CHECK(first == 3);
    CHECK(second == 3);

    CHECK(events.unsubscribe(a));
    CHECK(!events.unsubscribe(a));

    CHECK(events.emit("frame",Any(2)) == 1u);
    CHECK(first == 3);
    CHECK(second == 5);

    events.clear();
    CHECK(events.size() == 0u);
}

void testCallbackCanUnsubscribeWithoutDeadlock()
{
    nativeweb::detail::EventDispatcher events;
    nativeweb::detail::EventSubscriptionId id=0;
    int calls=0;

    id=events.subscribe("once",[&](const Any&){
        ++calls;
        events.unsubscribe(id);
    });

    CHECK(events.emit("once",Any()) == 1u);
    CHECK(events.emit("once",Any()) == 0u);
    CHECK(calls == 1);
}

void testThrowingSubscriberDoesNotInterruptLaterSubscribers()
{
    nativeweb::detail::EventDispatcher events;
    int second = 0;
    events.subscribe("from-browser", [](const Any&) {
        throw std::runtime_error("user callback failed");
    });
    events.subscribe("from-browser", [&second](const Any& data) {
        second += AnyCast<int>(data);
    });
    bool propagated = false;
    try { CHECK(events.emit("from-browser", Any(5)) == 2u); }
    catch (...) { propagated = true; }
    CHECK(!propagated);
    CHECK(second == 5);
}

}

int main()
{
    testSubscribeEmitUnsubscribe();
    testCallbackCanUnsubscribeWithoutDeadlock();
    testThrowingSubscriberDoesNotInterruptLaterSubscribers();

    if(failures){std::cerr<<"Event dispatcher regression failed: "<<failures<<std::endl;return 1;}
    std::cout<<"Event dispatcher regression: PASS"<<std::endl;
    return 0;
}
