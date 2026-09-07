// Bounded transport-delay compensation. Allocate/reset only off the audio thread.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <memory>
namespace pipedal {
class CompensationDelay {
public:
    static constexpr uint32_t MaxDelay = 65536;
    static constexpr uint32_t FadeSamples = 64;
    void Prepare(size_t channels) { history.assign(channels, std::vector<float>(MaxDelay+1,0)); Reset(); }
    void Reset() { for(auto& h:history) std::fill(h.begin(),h.end(),0); pos=0; current=target=requested=0; fade=0; first=true; limited=false; }
    void SetDelay(uint64_t delay) {
        limited=delay>MaxDelay;
        requested=uint32_t(std::min<uint64_t>(delay,MaxDelay));
        if(first) {current=target=requested;first=false;}
    }
    bool Limited() const {return limited;}
    // Channels advance together. Input and output may alias, including mono
    // references fanned out into stereo; callers provide one entry per channel.
    void Process(float* const* input,float* const* output,size_t channels,uint32_t frames) {
        for(uint32_t i=0;i<frames;++i) {
            if(!fade && current!=requested) {target=requested;fade=FadeSamples;}
            const float mix=fade?float(FadeSamples-fade+1)/FadeSamples:0;
            for(size_t ch=0;ch<channels;++ch) history[ch][pos]=input[ch][i];
            for(size_t ch=0;ch<channels;++ch) {
                const float a=history[ch][(pos+MaxDelay+1-current)%(MaxDelay+1)];
                const float b=history[ch][(pos+MaxDelay+1-target)%(MaxDelay+1)];
                output[ch][i]=fade?a+mix*(b-a):a;
            }
            if(fade && --fade==0)current=target;
            pos=(pos+1)%(MaxDelay+1);
        }
    }
private:
    std::vector<std::vector<float>> history;
    uint32_t pos=0,current=0,target=0,requested=0,fade=0;
    bool first=true,limited=false;
};
// Owned by the prepared graph. Read/written only in topological audio order.
struct PathLatency {
    uint64_t samples=0;
    bool limited=false;
    void SetSerial(const PathLatency& input, uint32_t effectSamples, bool effectLimited) {
        samples=input.samples+effectSamples;
        limited=input.limited || effectLimited;
    }
};

// The same merge object is used by the host action graph and offline tests.
// Owns buffers and nodes for the prepared graph's lifetime; Process only reads
// their existing storage and never changes ownership.
class LatencyMerge {
public:
    LatencyMerge(std::vector<float*> top, std::vector<float*> bottom,
                 std::vector<float*> topOutput, std::vector<float*> bottomOutput,
                 std::shared_ptr<PathLatency> topTime, std::shared_ptr<PathLatency> bottomTime,
                 std::shared_ptr<PathLatency> outputTime)
        : top_(std::move(top)), bottom_(std::move(bottom)),
          topOutput_(std::move(topOutput)), bottomOutput_(std::move(bottomOutput)),
          topTime_(std::move(topTime)), bottomTime_(std::move(bottomTime)), outputTime_(std::move(outputTime)) {
        topDelay_.Prepare(top_.size());
        bottomDelay_.Prepare(bottom_.size());
    }
    void Reset() { topDelay_.Reset(); bottomDelay_.Reset(); }
    void Process(uint32_t frames) {
        outputTime_->samples=std::max(topTime_->samples,bottomTime_->samples);
        topDelay_.SetDelay(outputTime_->samples-topTime_->samples);
        bottomDelay_.SetDelay(outputTime_->samples-bottomTime_->samples);
        topDelay_.Process(top_.data(),topOutput_.data(),top_.size(),frames);
        bottomDelay_.Process(bottom_.data(),bottomOutput_.data(),bottom_.size(),frames);
        outputTime_->limited=topTime_->limited || bottomTime_->limited || topDelay_.Limited() || bottomDelay_.Limited();
    }
private:
    std::vector<float*> top_,bottom_,topOutput_,bottomOutput_;
    std::shared_ptr<PathLatency> topTime_,bottomTime_,outputTime_;
    CompensationDelay topDelay_,bottomDelay_;
};
}
