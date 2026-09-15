#!/usr/bin/env python3
"""Portable save regression: compile production bank/storage/model logic with
small pedalboard, locale and I/O doubles (no Linux audio dependencies).
Run: python3 test/preset_save_regression.py. Full host/I/O checks still need Linux.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


banks = re.sub(r'^#include .*$', '', (root / 'src/Banks.hpp').read_text(), flags=re.M)
source = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "PiPedalException.hpp"
#define DECLARE_JSON_MAP(T)
namespace pipedal {
struct Pedalboard {
    std::string name_; int value = 0;
    const std::string& name() const { return name_; }
    void name(const std::string& n) { name_ = n; }
    static Pedalboard MakeDefault() { return {}; }
    Pedalboard DeepCopy() const { return *this; }
};
struct Locale {
    static auto GetInstance() { return std::make_shared<Locale>(); }
    auto GetCollator() { return GetInstance(); }
    int Compare(std::string a, std::string b) {
        for (auto& c : a) c = std::tolower(static_cast<unsigned char>(c));
        for (auto& c : b) c = std::tolower(static_cast<unsigned char>(c));
        return a.compare(b);
    }
};
}
''' + banks + r'''
using namespace pipedal;
BankFile copyBank(const BankFile& b) {
    BankFile r; r.name(b.name()); r.selectedPreset(b.selectedPreset());
    r.nextInstanceId(b.nextInstanceId());
    for (const auto& e : b.presets()) r.presets().push_back(std::make_unique<BankFileEntry>(*e));
    return r;
}
struct Storage {
    BankFile currentBank, disk; BankIndex bankIndex; bool failWrite = false;
    void SaveBankFile(const std::string&, const BankFile& b) {
        if (failWrite) throw std::runtime_error("disk full");
        disk = copyBank(b);
    }
    void LoadBankFile(const std::string&, BankFile* b) { *b = copyBank(disk); }
    void SaveCurrentPreset(const Pedalboard&);
    int64_t SaveCurrentPresetAs(const Pedalboard&, int64_t, const std::string&, int64_t, int64_t);
    void GetPresetIndex(PresetIndex*);
    std::vector<PresetIndexEntry> RequestBankPresets(int64_t);
};
struct PiPedalModel {
    std::recursive_mutex mutex; Storage storage; Pedalboard pedalboard;
    bool changed = true; int boards = 0, lists = 0;
    void SyncLv2State() {}
    void PrepareSnapshostsForSave(Pedalboard&) {}
    void UpdateVst3Settings(Pedalboard&) {}
    void SetPresetChanged(int64_t, bool v) { changed = v; }
    void FirePedalboardChanged(int64_t, bool audio) { assert(!audio); ++boards; }
    void FirePresetsChanged(int64_t) { ++lists; }
    int64_t SaveCurrentPresetAs(int64_t, int64_t, const std::string&, int64_t, int64_t);
};
'''
for signature in ('static void SortPresetEntries(', 'void Storage::SaveCurrentPreset(const Pedalboard&',
                  'int64_t Storage::SaveCurrentPresetAs(', 'void Storage::GetPresetIndex(',
                  'std::vector<PresetIndexEntry> Storage::RequestBankPresets('):
    source += function('src/Storage.cpp', signature) + '\n'
source += function('src/PiPedalModel.cpp', 'int64_t PiPedalModel::SaveCurrentPresetAs(')
source += r'''
// Exercise the real filesystem rollback path with a tiny serializer double.
bool failSerialize = false;
struct json_writer {
    std::ostream& stream;
    json_writer(std::ostream& s, bool) : stream(s) {}
    void write(const BankFile&) { stream << "replacement"; if (failSerialize) stream.setstate(std::ios::badbit); }
};
void FileSystemSync() {}
struct DiskStorage {
    std::filesystem::path path;
    std::filesystem::path GetBankFileName(const std::string&) { return path; }
    void SaveBankFile(const std::string&, const BankFile&);
};
'''
source += function('src/Storage.cpp', 'void Storage::SaveBankFile(').replace('Storage::', 'DiskStorage::', 1)
source += r'''
template<class F> void fails(F f) { bool failed = false; try { f(); } catch (...) { failed = true; } assert(failed); }
int main(int argc, char** argv) {
    assert(argc == 2);
    DiskStorage files; files.path = std::filesystem::path(argv[1]) / "test.bank";
    { std::ofstream old(files.path); old << "original"; }
    BankFile empty;
    failSerialize = true;
    fails([&] { files.SaveBankFile("test", empty); });
    { std::ifstream restored(files.path); std::string value; restored >> value; assert(value == "original"); }
    assert(!std::filesystem::exists(files.path.string() + ".$$$"));
    failSerialize = false;
    files.SaveBankFile("test", empty);
    { std::ifstream saved(files.path); std::string value; saved >> value; assert(value == "replacement"); }
    // An open failure must propagate as well.
    files.path = std::filesystem::path(argv[1]) / "missing" / "test.bank";
    fails([&] { files.SaveBankFile("test", empty); });
    PiPedalModel m; auto& s = m.storage;
    for (int id : {1, 2}) { BankIndexEntry b; b.instanceId(id); b.name(std::to_string(id)); s.bankIndex.entries().push_back(b); }
    s.bankIndex.selectedBank(1);
    Pedalboard original; original.name("Zulu"); original.value = 10;
    auto originalId = s.currentBank.addPreset(original);
    s.currentBank.selectedPreset(originalId);
    m.pedalboard = original; m.pedalboard.value = 99;
    // A stale insertion anchor still saves; state/name update without audio reload.
    auto saved = m.SaveCurrentPresetAs(0, 1, "alpha", 99999, -1);
    assert(s.currentBank.hasItem(saved) && s.disk.getItem(saved).preset().value == 99);
    assert(!m.changed && m.pedalboard.name() == "alpha" && m.boards == 1);
    assert(s.currentBank.selectedPreset() == saved);
    PresetIndex index; s.GetPresetIndex(&index);
    assert(index.presets()[0].name() == "alpha" && index.selectedInstanceId() == saved);
    assert(s.currentBank.presets()[0]->instanceId() == originalId); // MIDI position preserved.
    assert(s.RequestBankPresets(1)[0].name() == "alpha");
    // Collision must fail without loading, discarding edits or clearing dirty state.
    m.changed = true; m.pedalboard.value = 123;
    fails([&] { m.SaveCurrentPresetAs(0, 1, "Zulu", saved, -1); });
    assert(m.changed && m.pedalboard.value == 123 && s.currentBank.selectedPreset() == saved);
    assert(s.currentBank.getItem(originalId).preset().value == 10);
    // Explicit overwrite retains the target ID and replaces its content.
    assert(m.SaveCurrentPresetAs(0, 1, "Zulu", saved, originalId) == originalId);
    assert(s.disk.getItem(originalId).preset().value == 123 && !m.changed);
    assert(s.currentBank.presets().size() == 2);
    // Rename/delete between confirmation and save rejects the stale target.
    fails([&] { m.SaveCurrentPresetAs(0, 1, "renamed", -1, originalId); });
    fails([&] { m.SaveCurrentPresetAs(0, 1, "Zulu", -1, 99999); });
    // Failed new save, overwrite and ordinary save leave saved data/selection intact.
    m.changed = true; m.pedalboard.value = 456; s.failWrite = true;
    for (auto target : {-1LL, static_cast<long long>(originalId)})
        fails([&] { m.SaveCurrentPresetAs(0, 1, target == -1 ? "new" : "Zulu", -1, target); });
    fails([&] { s.SaveCurrentPreset(m.pedalboard); });
    assert(m.changed && m.pedalboard.value == 456 && s.currentBank.selectedPreset() == originalId);
    assert(s.currentBank.presets().size() == 2 && s.currentBank.getItem(originalId).preset().value == 123);
    // Saving/overwriting another bank preserves the source and its dirty flag.
    s.failWrite = false; int boards = m.boards;
    assert(m.SaveCurrentPresetAs(0, 2, "remote", -1, -1) == -1);
    assert(m.changed && m.pedalboard.name() == "Zulu" && m.boards == boards);
    auto remoteId = s.disk.getPresetByName("remote")->instanceId();
    assert(m.SaveCurrentPresetAs(0, 2, "remote", -1, remoteId) == -1);
    assert(s.disk.getItem(remoteId).preset().value == 456);
    assert(s.currentBank.presets().size() == 2);
}
'''
with tempfile.TemporaryDirectory(prefix='pipedal-presets-') as directory:
    cpp = Path(directory) / 'regression.cpp'
    cpp.write_text(source)
    binary = Path(directory) / 'regression'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wno-pragma-once-outside-header',
                    '-I', str(root / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary), directory], check=True)
print('Preset bank/storage/model regression checks passed (hardware and I/O doubles).')
