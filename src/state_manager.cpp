#include "state_manager.h"
#include <Preferences.h>

static const uint32_t SNAPSHOT_MAGIC = 0x7A20C0DE;
static const char* NVS_NAMESPACE = "tarot";

RTC_DATA_ATTR static UiSnapshot rtcSnapshot;

void saveSnapshotToRtc(const UiSnapshot& snap) {
    rtcSnapshot = snap;
    rtcSnapshot.magic = SNAPSHOT_MAGIC;
}

bool loadSnapshotFromRtc(UiSnapshot& snap) {
    if (rtcSnapshot.magic != SNAPSHOT_MAGIC) return false;
    snap = rtcSnapshot;
    return true;
}

void saveModeToNvs(DrawMode mode) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("mode", (uint8_t)mode);
    prefs.end();
}

DrawMode loadModeFromNvs() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t m = prefs.getUChar("mode", MODE_SINGLE);
    prefs.end();
    return m == MODE_SPREAD ? MODE_SPREAD : MODE_SINGLE;
}

void saveLastReadingToNvs(const Reading& r) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBytes("last", &r, sizeof(Reading));
    prefs.end();
}

bool loadLastReadingFromNvs(Reading& r) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t n = prefs.getBytesLength("last") == sizeof(Reading)
                   ? prefs.getBytes("last", &r, sizeof(Reading))
                   : 0;
    prefs.end();
    if (n != sizeof(Reading) || r.count == 0 || r.count > SPREAD_SIZE) {
        r.count = 0;
        return false;
    }
    for (uint8_t i = 0; i < r.count; i++) {
        if (r.cards[i] >= 78) { r.count = 0; return false; }
    }
    return true;
}

