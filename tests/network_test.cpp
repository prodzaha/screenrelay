#include "../src/network.hpp"
#include <iostream>
#include <stdexcept>
int main(){
    auto test=[](bool ok,const char* name){if(!ok)throw std::runtime_error(name);};
    const std::string on=R"({"device":{"modelName":"TEST_TV","id":"test-id","PowerState":"on","type":"Samsung SmartTV"}})";
    test(relay::parseSignal(on,"TEST_TV","test-id")==relay::Signal::On,"verified on");
    test(relay::parseSignal(on,"OTHER","test-id")==relay::Signal::Invalid,"wrong model must be invalid");
    test(relay::parseSignal(on,"TEST_TV","other-id")==relay::Signal::Invalid,"wrong ID must be invalid");
    test(relay::parseSignal("not json","TEST_TV","test-id")==relay::Signal::Invalid,"malformed body must be invalid");
    test(relay::parseSignal(R"({"device":{"modelName":"TEST_TV","id":"test-id","PowerState":"off","type":"Samsung SmartTV"}})","TEST_TV","test-id")==relay::Signal::Off,"verified off");
    test(relay::parseSignal(R"({"device":{"modelName":"TEST_TV","id":"test-id","PowerState":"standby","type":"Samsung SmartTV"}})","TEST_TV","test-id")==relay::Signal::Invalid,"unknown power must not look like off");
    test(relay::localAddress("192.168.1.5"),"private local address");
    test(!relay::localAddress("8.8.8.8"),"public endpoint is not a TV on LAN");
    test(!relay::localAddress("192.168.0.1/path"),"reject URL injection");
    std::cout<<"PASS: network identity, malformed responses, explicit off, LAN-only addresses\n";
}
