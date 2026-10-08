#include "../src/controller.hpp"
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
    relay::SampleQueue queue;
    check(queue.push({relay::Signal::On,1,1,0}),"queue first sample");
    check(queue.push({relay::Signal::Invalid,1,2,0}),"queue safety signal");
    check(queue.push({relay::Signal::On,1,3,0}),"queue third sample");
    check(queue.take()->signal==relay::Signal::On,"ordered first sample");
    check(queue.take()->signal==relay::Signal::Invalid,"invalid response cannot be overwritten");
    check(queue.take()->signal==relay::Signal::On,"ordered third sample");
    for(int i=0;i<16;++i)check(queue.push({relay::Signal::On,1,(unsigned long long)i,0}),"bounded queue capacity");
    check(!queue.push({relay::Signal::On,1,17,0}),"overflow must be reported, not silently drop evidence");
    queue.clear();check(!queue.take(),"clear resets queue");
    relay::Controller c;
    c.manual(relay::Action::Monitor,relay::Signal::On);
    for(int i=0;i<6;++i)check(c.feed(relay::Signal::On)==relay::Action::None,"manual monitor persists while TV stays on");
    c.committed(relay::Action::Monitor);
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"one timeout does not release manual hold");
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"off transition ends hold with monitor already active");
    check(c.feed(relay::Signal::On)==relay::Action::None,"on transition debounces after manual hold");
    check(c.feed(relay::Signal::On)==relay::Action::Tv,"next on transition selects TV");
    c.manual(relay::Action::Tv,relay::Signal::On);
    c.committed(relay::Action::Tv);c.clearEvidence();
    check(c.feed(relay::Signal::On)==relay::Action::None,"topology/evidence refresh preserves manual hold");
    check(c.feed(relay::Signal::On)==relay::Action::None,"unchanged on preserves hold");
    check(c.feed(relay::Signal::Off)==relay::Action::Monitor,"F12 still returns to monitor on explicit off");
    c.manual(relay::Action::Both,relay::Signal::NoReply);
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"unknown baseline first timeout");
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"first stable off establishes baseline without undoing both");
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"unchanged off keeps both");
    check(c.feed(relay::Signal::On)==relay::Action::None,"next on first reply");
    check(c.feed(relay::Signal::On)==relay::Action::Tv,"next on releases both override");
    c.manual(relay::Action::Both,relay::Signal::On);
    check(c.feed(relay::Signal::Invalid)==relay::Action::Pause,"manual hold must not mask unsafe identity");
    c.reset(relay::Action::None);
    check(c.feed(relay::Signal::On)==relay::Action::None,"single on must not switch");
    check(c.feed(relay::Signal::On)==relay::Action::Tv,"two on responses must select TV");
    c.committed(relay::Action::Tv);
    check(c.feed(relay::Signal::On)==relay::Action::None,"unchanged state must not reapply");
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"single missed response must not switch");
    check(c.feed(relay::Signal::On)==relay::Action::None,"successful response resets failures");
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"first new failure must not switch");
    check(c.feed(relay::Signal::NoReply)==relay::Action::Monitor,"second consecutive failure must select monitor");
    c.committed(relay::Action::Monitor);
    check(c.feed(relay::Signal::On)==relay::Action::None,"return to TV requires debounce");
    check(c.feed(relay::Signal::Invalid)==relay::Action::Pause,"invalid identity must pause");
    c.reset(relay::Action::Monitor);
    check(c.feed(relay::Signal::On)==relay::Action::None,"reset clears debounce");
    check(c.feed(relay::Signal::On)==relay::Action::Tv,"second on after reset selects TV");
    c.committed(relay::Action::Tv);
    check(c.feed(relay::Signal::Off)==relay::Action::Monitor,"explicit off selects monitor");
    c.committed(relay::Action::Monitor);
    check(c.feed(relay::Signal::Off)==relay::Action::None,"unchanged off must not reapply");
    c.reset(relay::Action::None);
    check(c.feed(relay::Signal::NoReply)==relay::Action::None,"startup miss is debounced");
    check(c.feed(relay::Signal::NoReply)==relay::Action::Monitor,"startup two misses selects validated monitor");
    std::cout << "PASS: controller state transitions, debounce, identity failure, unchanged state\n";
}
