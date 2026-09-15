#!/usr/bin/env python3
"""Compile the production OLED renderer/worker against a fake I2C sink.
Run: python3 test/oled_regression.py [directory-for-PBM-preview-frames]
"""
from pathlib import Path
import os, subprocess, tempfile, sys
root = Path(__file__).resolve().parents[1]
gpio = (root / 'src/Gpio.cpp').read_text()

def method(signature):
    start = gpio.index(signature)
    brace = gpio.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (gpio[end] == '{') - (gpio[end] == '}')
        end += 1
    return gpio[start:end].replace(' override', '')

renderer = gpio[gpio.index('    class Ssd1306Display\n'):gpio.index('    class Ht16k33Matrix\n')]
utf8_source = (root / 'PiPedalCommon/src/Utf8Utils.cpp').read_text()
utf8_increment = utf8_source[utf8_source.index('    size_t Utf8Increment('):utf8_source.index('    size_t Utf8Decrement(')]
source = r'''
#include "Gpio.hpp"
#include "SuprDuprBootLogo.hpp"
#include "Utf8Utils.hpp"
#include <cassert>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <fstream>
using namespace pipedal;
struct Lv2Log { template<typename... T> static void warning(T...) {} };
struct I2cDevice {
    inline static int writes = 0;
    inline static bool connected = true;
    bool Open(const std::string&, int, std::string*) { return connected; }
    bool Write(const uint8_t*, size_t, std::string*) { ++writes; return connected; }
};
''' + 'namespace pipedal { namespace implementation { void Utf8RangeError() { throw std::out_of_range("UTF8"); } }' + utf8_increment + '}\n' + renderer + r'''
class Worker {
public:
    using WaveformProvider = GpioManager::WaveformProvider;
    using TunerSampleProvider = GpioManager::TunerSampleProvider;
    std::mutex callbackMutex_;
    WaveformProvider waveformProvider_;
    TunerSampleProvider tunerSampleProvider_;
    std::atomic<bool> redrawRequested_ = false;
''' + gpio[gpio.index('        mutable std::mutex displayMutex_;'):gpio.index('        std::unique_ptr<std::jthread> inputThread_;')]
for signature in ['void ShowLoadedPreset(', 'void DismissBootLogo(', 'void ClearDisplayMessage(',
                  'void ShowTemporaryMenu(', 'void ShowDisplayMessage(', 'void ShowTemporaryControlDashboard(',
                  'void ShowControlDashboard(', 'void SetDisplayMode(']:
    source += method(signature) + '\n'
# Compile the worker's Linux rendering branch on macOS as well.
process = method('bool ProcessDisplay(')
process = process[:process.index('#else')] + '\n}\n'
source += process.replace('#if defined(__linux__)', '') + '};\n'
# Compile the production mapping-to-overlay entry point against the same worker.
model = (root / 'src/PiPedalModel.cpp').read_text()
show = model[model.index('void PiPedalModel::ShowGpioBindingValue('):model.index('PedalboardItem *PiPedalModel::GetPedalboardItemForFileProperty(')]
source += r'''
class PiPedalModel {
public:
    Worker *gpioManager;
    std::recursive_mutex mutex;
    struct Item { std::string title_, pluginName_; bool isEnabled() { return true; } };
    struct Board { Item *GetItem(int64_t) { return nullptr; } } pedalboard;
    template<typename... T> GpioDisplayControl BuildGpioDisplayControl(T...) {
        GpioDisplayControl c; c.label="GAIN"; c.value="0.5"; return c;
    }
    void ShowGpioBindingValue(const GpioBinding&, const GpioInputEvent&, std::optional<float>);
};
''' + show
source += r'''
void save(const Ssd1306Display& d, const std::string& file) {
    if (file.empty()) return;
    std::ofstream out(file, std::ios::binary); out << "P4\n128 64\n";
    for (int y=0;y<64;++y) for (int group=0;group<16;++group) {
        unsigned char byte=0;
        for(int bit=0;bit<8;++bit) if(!(d.FrameBuffer()[(y/8)*128+group*8+bit]&(1<<(y%8)))) byte |= 128>>bit;
        out.put(byte);
    }
}
int main(int argc, char**argv) {
    std::string dir = argc>1 ? argv[1] : "";
    auto preview=[&](const Ssd1306Display& d,const std::string& name){if(!dir.empty()) save(d,dir+"/"+name+".pbm");};
    OledArtwork artwork; artwork.bytes.fill(255);
    std::ostringstream encoded; json_writer(encoded).write(artwork);
    OledArtwork decoded; std::istringstream input(encoded.str()); json_reader(input).read(&decoded);
    assert(decoded.bytes==artwork.bytes);
    std::vector<unsigned int> legacy(384);
    for(size_t i=0;i<legacy.size();++i) legacy[i]=unsigned(i%256);
    std::ostringstream legacyEncoded; json_writer(legacyEncoded).write(legacy);
    OledArtwork migrated; std::istringstream legacyInput(legacyEncoded.str()); json_reader(legacyInput).read(&migrated);
    for(size_t y=0;y<40;++y) for(size_t x=0;x<16;++x)
        assert(migrated.bytes[y*16+x]==legacy[(y*24/40)*16+x]);
    auto bad=[&](std::string s){ bool threw=false; try { std::istringstream in(s); json_reader(in).read(&decoded); }
        catch(const std::exception&e){threw=std::string(e.what()).find("640 integers")!=std::string::npos;} assert(threw); };
    bad("[]"); bad("[256]"); bad("[-1]"); bad("[0.5]"); bad("[NaN]"); bad("[true]"); bad("[\"255\"]");
    bad(encoded.str().substr(0,encoded.str().size()-1)+",0]");
    bad("[999999999999999999999999]");
    Ssd1306Display display; GpioTunerFrame tuner;
    display.DrawTuner(tuner); const auto base=display.FrameBuffer();
    for(int width : {0,64,128}) {
        display.DrawTuner(tuner); display.DrawPresetNotice("Deep Bass",artwork,width);
        assert(std::equal(base.begin(),base.begin()+384,display.FrameBuffer().begin()));
        for(int y=24;y<64;++y) for(int x=0;x<128;++x)
            assert(bool(display.FrameBuffer()[(y/8)*128+x]&(1<<(y%8)))==(x<width));
        preview(display,"preset-"+std::to_string(width));
    }
    OledArtwork blankArtwork;
    display.DrawTuner(tuner); display.DrawPresetNotice("Deep Bass",blankArtwork,128);
    assert(std::all_of(display.FrameBuffer().begin()+384,display.FrameBuffer().end(),[](uint8_t b){return b==0;}));
    display.DrawTuner(tuner); display.DrawPresetNotice("Deep Bass",std::nullopt,128);
    assert(std::any_of(display.FrameBuffer().begin()+384,display.FrameBuffer().end(),[](uint8_t b){return b!=0;}));
    for(int width : {0,32,64,96,128}) { display.DrawBootLogo(width); preview(display,"boot-"+std::to_string(width)); }
    assert(Ssd1306Display::PresetNameLines("é🎸B")[0]=="??B");
    assert(Ssd1306Display::PresetNameLines(std::string(100,'A'))[1].ends_with("..."));
    assert(Ssd1306Display::PresetNameLines("")[0]=="UNTITLED");
    Worker worker; GpioDisplaySettings settings; settings.presetArtwork_=true; settings.swipeReveal_=true;
    worker.SetDisplayMode(GpioDisplayMode::Tuner);
    int analyses=0;
    worker.tunerSampleProvider_=[&](auto*,auto*,size_t,auto*) { ++analyses; return size_t(0); };
    auto next=std::chrono::steady_clock::time_point{}, overlay=next; uint64_t seen=0;
    auto draw=[&](auto now, bool force=true){
        const int before=I2cDevice::writes;
        assert(worker.ProcessDisplay(display,settings,now,force,&next,&seen,&overlay));
        if(force) assert(I2cDevice::writes-before==35); // one window command + 34 chunks, once per frame.
    };
    worker.ShowLoadedPreset(1,7,"FIRST",artwork);
    auto start=worker.presetNotice_.start;
    draw(start+std::chrono::milliseconds(150)); assert(analyses==1);
    assert(display.FrameBuffer()[3*128+63]==255 && display.FrameBuffer()[3*128+64]==0);
    const int writes=I2cDevice::writes; draw(start+std::chrono::milliseconds(151),false); assert(writes==I2cDevice::writes);
    worker.ShowLoadedPreset(2,7,"SECOND",std::nullopt); // same numeric ID across banks.
    assert(worker.presetNotice_.bankId==2 && worker.presetNotice_.name=="SECOND");
    auto second=worker.presetNotice_.start;
    draw(second+std::chrono::milliseconds(1999)); auto noticeFrame=display.FrameBuffer();
    draw(second+std::chrono::milliseconds(2000)); assert(display.FrameBuffer()==base); assert(noticeFrame!=base);
    worker.ShowLoadedPreset(2,8,"THIRD",artwork); start=worker.presetNotice_.start;
    settings.presetNameOnLoad_=false; draw(start+std::chrono::milliseconds(300));
    assert(display.FrameBuffer()[3*128]==255);
    settings.presetArtwork_=false; draw(start+std::chrono::milliseconds(300));
    assert(display.FrameBuffer()==base);
    settings.presetNameOnLoad_=true; settings.presetArtwork_=true;
    worker.SetDisplayMode(GpioDisplayMode::Blank); draw(start);
    for(auto b:display.FrameBuffer()) assert(b==0);
    worker.SetDisplayMode(GpioDisplayMode::Tuner);
    worker.ShowTemporaryMenu({}); assert(worker.presetNotice_.presetId==8);
    int before=analyses; draw(start); assert(analyses==before+1);
    worker.ShowLoadedPreset(3,9,"SELECTED",artwork); worker.ClearDisplayMessage();
    assert(worker.presetNotice_.presetId==9); draw(worker.presetNotice_.start+std::chrono::milliseconds(400));
    assert(display.FrameBuffer()[3*128]==255);
    worker.DismissBootLogo(); assert(worker.presetNotice_.presetId==9);
    worker.presetNotice_ = {}; // Separate the boot fixture from preset timing.
    draw(start); assert(display.FrameBuffer()==base);
    worker.bootUntil_=start+std::chrono::milliseconds(2200);
    draw(start+std::chrono::milliseconds(800)); auto completed=display.FrameBuffer();
    draw(start+std::chrono::milliseconds(2199)); assert(completed==display.FrameBuffer());
    draw(start+std::chrono::milliseconds(2200)); assert(display.FrameBuffer()==base);
    // Loading a mapped preset refreshes its input state. It must not replace
    // the loaded-name notice; real control/bypass feedback waits for expiry.
    PiPedalModel model{&worker}; GpioInputEvent event; GpioBinding binding;
    for (auto action : {GpioActionType::Control, GpioActionType::Bypass}) {
        binding.actionType_ = static_cast<int32_t>(action);
        worker.ClearDisplayMessage();
        worker.ShowLoadedPreset(4,10,"MAPPED PRESET",artwork);
        const auto sequence=worker.displayMessageSequence_;
        event.initial=true; model.ShowGpioBindingValue(binding,event,0.5f);
        assert(worker.presetNotice_.presetId==10);
        assert(worker.displayMessageSequence_==sequence);
        draw(worker.presetNotice_.start+std::chrono::milliseconds(400));
        assert(display.FrameBuffer()[3*128]==255);
        event.initial=false; model.ShowGpioBindingValue(binding,event,0.5f);
        assert(worker.presetNotice_.presetId==10);
        assert(worker.displayMessageSequence_==sequence+1);
        const auto loaded=worker.presetNotice_.start;
        draw(loaded+std::chrono::milliseconds(1999));
        assert(display.FrameBuffer()[3*128]==255);
        draw(loaded+std::chrono::milliseconds(2000));
        Ssd1306Display expected; expected.DrawMessage(worker.displayMessage_);
        assert(display.FrameBuffer()==expected.FrameBuffer());
    }
    // All overlay kinds wait; newer feedback replaces older pending feedback.
    // Even a timeout shorter than the notice starts only when feedback is shown.
    settings.overlayTimeoutMs_=500;
    worker.ShowLoadedPreset(5,11,"PRIORITY",artwork); start=worker.presetNotice_.start;
    worker.ShowTemporaryControlDashboard({});
    draw(start+std::chrono::milliseconds(1000)); assert(display.FrameBuffer()[3*128]==255);
    worker.ShowTemporaryMenu({});
    draw(start+std::chrono::milliseconds(1500)); assert(display.FrameBuffer()[3*128]==255);
    GpioDisplayMessage latest; latest.label="LATEST";
    worker.ShowDisplayMessage(latest); worker.DismissBootLogo();
    draw(start+std::chrono::milliseconds(1999)); assert(display.FrameBuffer()[3*128]==255);
    draw(start+std::chrono::milliseconds(2000)); auto feedback=display.FrameBuffer();
    Ssd1306Display expected; expected.DrawMessage(latest);
    assert(feedback==expected.FrameBuffer());
    draw(start+std::chrono::milliseconds(2499)); assert(display.FrameBuffer()==feedback);
    draw(start+std::chrono::milliseconds(2500)); assert(display.FrameBuffer()==base);
    worker.ShowTemporaryMenu({});
    worker.ShowLoadedPreset(5,12,"NEWEST",artwork); start=worker.presetNotice_.start;
    assert(worker.displayOverlayType_==0); // No stale feedback from the old preset.
    draw(start+std::chrono::milliseconds(2000)); assert(display.FrameBuffer()==base);
    I2cDevice::connected=false;
    assert(!worker.ProcessDisplay(display,settings,start,true,&next,&seen,&overlay));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'oled.cpp'; cpp.write_text(source)
    exe=Path(tmp)/'oled'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-ffast-math',
        '-I'+str(root/'src'),'-I'+str(root/'PiPedalCommon/src/include'),str(cpp),
        str(root/'PiPedalCommon/src/json.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*sys.argv[1:]],check=True)
print('OLED bitmap validation, framebuffer, timing, priority, UTF-8 and single-flush checks passed.')
