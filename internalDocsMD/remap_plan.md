Current Architecture
The CSV Remap flow:

LayoutLoader.cpp reads speaker layout JSON → SpeakerLayoutData
speakers[i].deviceChannel = the physical device channel from JSON "channel" key
For translab: consecutive 0..17
For Allosphere: starts at 1, likely has gaps
Spatializer::init() builds al::Dbap with speakers indexed 0..N-1 (consecutive)
Uses the loop index rather than speaker.deviceChannel — this is the critical fix
DBAP renders into mRenderIO using channels 0..N-1
Subwoofer LFE routes directly to subwooferData.deviceChannel in mRenderIO
Phase 7: OutputRemap (CSV) maps layout column (0-based consecutive) → device column (physical)
Without a CSV: identity mapping (ch 0 → ch 0, ch 1 → ch 1, etc.)
This works fine for translab with consecutive channels, but Allosphere's non-consecutive channels need a CSV
The core issue is that DBAP writes to channels by their array index, so if I passed deviceChannel directly, it would write out-of-bounds for Allosphere's gaps. The solution keeps DBAP using consecutive 0..N-1 internally and automatically remaps at the end of the signal chain instead of requiring a manual CSV.

I need to build the remap table from layout.speakers[i].deviceChannel during initialization, then resize the output buffer to accommodate the maximum device channel rather
