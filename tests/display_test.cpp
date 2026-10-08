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
    static_assert(sizeof(DISPLAYCONFIG_PATH_INFO)==72);static_assert(sizeof(DISPLAYCONFIG_MODE_INFO)==64);
    auto e=entry(),other=e;other.device.name="Changed EDID product name";
    check(e.device.same(other.device),"product/name change must not change physical connector");
    other.device.connector=2;check(!e.device.same(other.device),"different connector must not match");
    auto loaded=relay::loadProfile(relay::saveProfile({e}));
    auto tv=e;tv.device.technology=10;tv.device.name="other";tv.refresh={160,1};
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
