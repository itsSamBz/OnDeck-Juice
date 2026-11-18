// Platform-independent helpers shared by the drivers.
#include <cstdio>
#include <cwctype>

#include "hid.h"

std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", (unsigned)v);
    return b;
}

int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int MapTo100(int v, int lo, int hi) {
    if (hi <= lo) return Clamp100(v);
    return Clamp100((v - lo) * 100 / (hi - lo));
}

int Interpolate(int mv, const int* mvTable, const int* pctTable, int n) {
    if (mv >= mvTable[0]) return pctTable[0];
    for (int i = 1; i < n; i++) {
        if (mv >= mvTable[i]) {
            int span = mvTable[i - 1] - mvTable[i];
            return pctTable[i] + (pctTable[i - 1] - pctTable[i]) * (mv - mvTable[i]) / (span ? span : 1);
        }
    }
    return pctTable[n - 1];
}

DeviceKind GuessKind(const std::wstring& name) {
    std::wstring n = name;
    for (auto& ch : n) ch = towlower(ch);
    auto has = [&](const wchar_t* s) { return n.find(s) != std::wstring::npos; };
    if (has(L"mouse") || has(L"mice") || has(L"trackball")) return DeviceKind::Mouse;
    if (has(L"keyboard") || has(L"keys")) return DeviceKind::Keyboard;
    if (has(L"controller") || has(L"gamepad") || has(L"xbox") || has(L"dualsense") || has(L"dualshock") || has(L"joy-con"))
        return DeviceKind::Gamepad;
    if (has(L"head") || has(L"buds") || has(L"pods") || has(L"ear") || has(L"wh-") || has(L"wf-") || has(L"audio") ||
        has(L"speaker") || has(L"arctis") || has(L"cloud") || has(L"void") || has(L"virtuoso"))
        return DeviceKind::Headset;
    return DeviceKind::Other;
}

DeviceKind KindFromCollections(const std::vector<HidColl>& all, USHORT vid, USHORT pid) {
    bool mouse = false, pad = false, kbd = false;
    for (const auto& c : all) {
        if (c.vid != vid || c.pid != pid || c.page() != 0x01) continue;
        mouse |= c.usage() == 0x02;
        pad |= c.usage() == 0x04 || c.usage() == 0x05;
        kbd |= c.usage() == 0x06;
    }
    if (mouse) return DeviceKind::Mouse;
    if (pad) return DeviceKind::Gamepad;
    if (kbd) return DeviceKind::Keyboard;
    return DeviceKind::Other;
}
