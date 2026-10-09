#include "../src/display.hpp"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
relay::Entry entry(){
    relay::Entry e;e.device={"test-gpu","test-display",5,1,123};
    e.source.infoType=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;e.source.sourceMode.width=3840;e.source.sourceMode.height=2160;
    e.target.infoType=DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;e.refresh={120000,1001};e.rotation=DISPLAYCONFIG_ROTATION_IDENTITY;
    return e;
}
int main(){
    unsigned tick=0,checks=0;
    check(relay::confirmUntil([&]{return ++checks==3;},[&]{return tick;},[&]{tick+=50;},200),"verification allows a mode to settle without restoring immediately");
    tick=0;checks=0;
    check(!relay::confirmUntil([&]{++checks;return false;},[&]{return tick;},[&]{tick+=50;},200),"verification has a hard deadline");
    check(checks==5,"verification must stop checking at deadline");
    tick=0;checks=0;
    check(relay::confirmUntil([&]{if(++checks==1)throw std::runtime_error("temporary query failure");return true;},[&]{return tick;},[&]{tick+=50;},200),"temporary topology query failure must not trigger immediate display writes");
    static_assert(sizeof(DISPLAYCONFIG_PATH_INFO)==72);static_assert(sizeof(DISPLAYCONFIG_MODE_INFO)==64);
    auto e=entry(),other=e;other.device.name="Changed EDID product name";
    check(e.device.same(other.device),"product/name change must not change physical connector");
    other.device.connector=2;check(!e.device.same(other.device),"different connector must not match");
    auto loaded=relay::loadProfile(relay::saveProfile({e}));
    auto tv=e;tv.device.technology=10;tv.device.name="other";tv.refresh={160,1};
    auto rescue=relay::recoveryCandidates({tv},{e});
    check(rescue.size()==2&&rescue[0][0].device.same(e.device),"TV-only previous topology must recover to verified monitor first");
    rescue=relay::recoveryCandidates({e,tv},{e});check(rescue[0].size()==2,"previous desktop containing monitor remains first recovery choice");
    rescue=relay::recoveryCandidates({e},{e});check(rescue.size()==1,"do not apply same recovery profile twice");
    rescue=relay::recoveryCandidates({tv},{});check(rescue.size()==1,"first-time setup without verified monitor retains previous desktop recovery");
    rescue=relay::recoveryCandidates({}, {e});check(rescue.size()==1&&rescue[0].size()==1,"no active display must recover to verified monitor, never an empty topology");
    check(relay::consistentProfiles({e},{tv},{e,tv}),"same selected pair is accepted");
    check(!relay::consistentProfiles({e},{tv},{e,e}),"duplicated unrelated both profile is rejected");
    auto wrong=tv;wrong.refresh={60,1};check(!relay::consistentProfiles({e},{tv},{e,wrong}),"mismatched both frequency is rejected");
    check(relay::matches({e},loaded),"profile roundtrip preserves exact rational refresh");
    loaded[0].refresh={120,1};check(!relay::matches({e},loaded),"119.88 must not be substituted with 120");
    bool rejected=false;try{auto j=e.save();j["refreshDenominator"]=0;relay::Entry::load(j);}catch(...){rejected=true;}
    check(rejected,"invalid refresh denominator must be rejected");
    rejected=false;try{relay::loadProfile(relay::json::array());}catch(...){rejected=true;}check(rejected,"empty profile must be rejected");
    std::cout<<"PASS: connector identity, profile roundtrip, exact fractional refresh and corrupt profile rejection\n";
}
