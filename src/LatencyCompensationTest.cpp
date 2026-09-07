#include "LatencyCompensation.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace pipedal;
static std::vector<float> delay(const std::vector<float>& x,unsigned samples,const std::vector<unsigned>& blocks) {
 CompensationDelay d;d.Prepare(1);d.SetDelay(samples);std::vector<float> y(x.size());
 size_t off=0,b=0;while(off<x.size()){unsigned n=std::min<size_t>(blocks[b++%blocks.size()],x.size()-off);float* in=const_cast<float*>(x.data()+off);float* out=y.data()+off;d.Process(&in,&out,1,n);off+=n;}return y;
}
int main(){
 std::vector<float> x(4096,0);x[0]=1;
 auto wet=delay(x,23,{64});
 // Reproduce the old bypass and split mix: separate half-height impulses.
 assert(.5f*x[0]+.5f*wet[0]==.5f && .5f*x[23]+.5f*wet[23]==.5f);
 for(unsigned n:{0,1,15,23,64,333}) {
  auto y=delay(x,n,{1,127,32,64,7});assert(y[n]==1);
  for(size_t i=0;i<y.size();i++)assert(y[i]==(i==n?1:0));
 }
 auto top=delay(delay(x,23,{37}),64,{81});
 auto bottom=delay(x,15,{13});
 bottom=delay(bottom,72,{3,32,64});assert(top==bottom);
 // Inner unequal split (23 vs 15) then 64 serial, outer branch 120.
 auto inner=delay(delay(x,15,{64}),8,{32});assert(inner==wet);
 auto nested=delay(delay(inner,64,{7}),33,{1,128});assert(nested==delay(x,120,{11}));
 auto bypass=delay(x,23,{64});
 for(float blend:{0.f,.25f,.5f,.75f,1.f})for(size_t i=0;i<x.size();i++)assert(blend*wet[i]+(1-blend)*bypass[i]==wet[i]);
 // Execute the production host merge with unequal and nested serial paths.
 auto t=std::make_shared<PathLatency>(),b=std::make_shared<PathLatency>(),m=std::make_shared<PathLatency>();
 t->samples=23;b->samples=15;
 auto bAudio=delay(x,15,{64});std::vector<float>ta(x.size()),ba(x.size());
 LatencyMerge merge({wet.data()},{bAudio.data()},{ta.data()},{ba.data()},t,b,m);
 merge.Process(x.size());assert(ta==ba && m->samples==23 && !m->limited);
 auto serial=std::make_shared<PathLatency>();serial->SetSerial(*m,64,false);
 auto other=std::make_shared<PathLatency>();other->samples=120;
 auto total=std::make_shared<PathLatency>();auto serialAudio=delay(ta,64,{33}),otherAudio=delay(x,120,{7});
 std::vector<float>na(x.size()),nb(x.size());
 LatencyMerge outer({serialAudio.data()},{otherAudio.data()},{na.data()},{nb.data()},serial,other,total);
 outer.Process(x.size());assert(na==nb && total->samples==120);
 other->samples=1000000;outer.Process(0);assert(total->limited); // bounded, explicitly flagged
 other->samples=120;outer.Reset();outer.Process(x.size());assert(na==nb && !total->limited);
 CompensationDelay d;d.Prepare(1);d.SetDelay(0);
 float in[256],out[256];std::fill_n(in,256,1);float* ip=in;float* op=out;d.Process(&ip,&op,1,256);
 d.SetDelay(23);d.Process(&ip,&op,1,256);for(float v:out)assert(v==1); // changing taps cannot boost DC
 d.SetDelay(UINT64_MAX);assert(d.Limited());d.Process(&ip,&op,1,256);for(float v:out)assert(std::isfinite(v)&&v>=0&&v<=1);
 d.Reset();d.SetDelay(0);d.Process(&ip,&ip,1,256);for(float v:in)assert(v==1);
 std::cout<<"PASS latency: old split/bypass defect reproduced; impulses, nested/serial paths, bypass blends, irregular/in-place, changes, bounds/reset\n";
}
