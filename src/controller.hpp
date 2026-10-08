#pragma once
#include <cstdint>
#include <deque>
#include <optional>
namespace relay {
enum class Signal { On, Off, NoReply, Invalid };
enum class Action { None, Monitor, Tv, Both, Pause };
struct Sample { Signal signal=Signal::NoReply; unsigned generation=0; uint64_t sequence=0,time=0; };
class SampleQueue {
    std::deque<Sample> samples_;
public:
    bool push(Sample sample){if(samples_.size()>=16)return false;samples_.push_back(sample);return true;}
    std::optional<Sample> take(){if(samples_.empty())return {};auto sample=samples_.front();samples_.pop_front();return sample;}
    void clear(){samples_.clear();}
};
class Controller {
    int on_ = 0, misses_ = 0;
    Action current_ = Action::None;
    bool manualHold_=false;
    std::optional<bool> baseline_;
    Action stable(bool on) {
        if(manualHold_) {
            if(!baseline_){baseline_=on;return Action::None;}
            if(*baseline_==on)return Action::None;
            manualHold_=false;baseline_.reset();
        }
        auto target=on?Action::Tv:Action::Monitor;
        return current_!=target?target:Action::None;
    }
public:
    void clearEvidence(){on_=misses_=0;}
    void reset(Action current) { current_=current;clearEvidence();manualHold_=false;baseline_.reset(); }
    void manual(Action current,Signal initial) {
        current_=current;clearEvidence();manualHold_=true;baseline_.reset();
        if(initial==Signal::On)baseline_=true;else if(initial==Signal::Off)baseline_=false;
    }
    void committed(Action action) { current_=action; }
    Action feed(Signal signal) {
        if (signal==Signal::Invalid) { on_=misses_=0; return Action::Pause; }
        if (signal==Signal::On) {
            misses_=0; if(on_<2)++on_;
            return on_==2 ? stable(true) : Action::None;
        }
        on_=0;
        if(signal==Signal::Off) { misses_=0; return stable(false); }
        if(misses_<2)++misses_;
        return misses_==2 ? stable(false) : Action::None;
    }
};
}
