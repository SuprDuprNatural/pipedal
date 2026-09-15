#!/usr/bin/env python3
"""Exercise the production GPIO command dispatcher without audio or hardware."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/PiPedalModel.cpp').read_text()
commands=s[s.index('void PiPedalModel::ExecuteGpioBinding('):s.index('    if ((mode == GpioBindingMode::Toggle')]+ '}\n'
source=r'''
#include "Gpio.hpp"
#include <mutex>
#include <cassert>
using namespace pipedal;
struct PiPedalModel {
    using clock=std::chrono::steady_clock;
    clock::time_point gpioPresetChangeAllowedAt{};
    std::recursive_mutex mutex;
    int loads=0, snapshots=0;
    void LoadPreset(int,int64_t) { ++loads; }
    void NextPreset() { ++loads; } void PreviousPreset() { ++loads; }
    void NextBank() { ++loads; } void PreviousBank() { ++loads; }
    void SetSnapshot(int64_t) { ++snapshots; }
    void NextSnapshot() { ++snapshots; } void PreviousSnapshot() { ++snapshots; }
    void ExecuteGpioBinding(const GpioBinding&,const GpioInputEvent&);
};
''' + commands + r'''
int main() {
    PiPedalModel m; GpioBinding b; GpioInputEvent e; e.risingEdge=true;
    for (auto action:{GpioActionType::LoadPreset,GpioActionType::NextPreset,
        GpioActionType::PreviousPreset,GpioActionType::NextBank,GpioActionType::PreviousBank}) {
        b.actionType_=static_cast<int>(action); m.gpioPresetChangeAllowedAt={};
        const int before=m.loads; auto start=PiPedalModel::clock::now();
        m.ExecuteGpioBinding(b,e); assert(m.loads==before+1);
        assert(m.gpioPresetChangeAllowedAt>=start+std::chrono::milliseconds(100));
        for(int bounce=0;bounce<5;++bounce) m.ExecuteGpioBinding(b,e);
        assert(m.loads==before+1);
        b.actionType_=static_cast<int>(GpioActionType::NextSnapshot);
        m.ExecuteGpioBinding(b,e); assert(m.snapshots>0); // Other actions are unaffected.
        b.actionType_=static_cast<int>(action);
        m.gpioPresetChangeAllowedAt=PiPedalModel::clock::now()-std::chrono::milliseconds(1);
        e.risingEdge=false; m.ExecuteGpioBinding(b,e); assert(m.loads==before+1);
        e.risingEdge=true; m.ExecuteGpioBinding(b,e); assert(m.loads==before+2);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'test.cpp';cpp.write_text(source);exe=Path(tmp)/'test'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I'+str(root/'src'),
        '-I'+str(root/'PiPedalCommon/src/include'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('GPIO preset cooldown: duplicate presses blocked, expiry and other actions passed.')
