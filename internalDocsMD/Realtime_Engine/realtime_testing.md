# 3 - 27 - 26 Realtime Testing

## TESTING ROUND 3

## Pre agent test notes 3:

Patch locations:

Render-side: Spatializer.hpp:654–683
Device-side: Spatializer.hpp:754–775
What changed (both blocks identically):

Added maxMainMs — tracked in the existing per-channel loop with a one-line !isSubwooferChannel guard.
domThresh now references maxMainMs instead of maxMs — so a loud sub no longer inflates the threshold and causes main channels to drop out.
The domMask bit-setting loop now skips sub channels — so subs can never appear in domMask, and a sub crossing threshold cannot produce a DOM event.
No new atomics. subRmsTotal already carries sub state. maxMs is kept for the absolute-mask path (unchanged).

If DOM events remain after this patch:

Yes, they would be meaningful. domMask at that point only reflects bits among the 16 mains, referenced against the loudest main channel. A change in that mask means the dominant main-speaker cluster genuinely shifted — not a sub threshold crossing, not a bleed edge artifact. Any remaining event with the same bit-count (16 bits in both prev and next) would be real spatial energy moving between main speakers and is the right thing to correlate against pop timestamps.

- ascent test 1:

* channels dissapeared around 168
* returned 201 but high pitched and only some channels, some poping
* result:
  Parameter gain(gain)[N2al9ParameterE] : /realtime/gain
  Parameter focus(focus)[N2al9ParameterE] : /realtime/focus
  Parameter speaker_mix_db(speaker_mix_db)[N2al9ParameterE] : /realtime/speaker_mix_db
  Parameter sub_mix_db(sub_mix_db)[N2al9ParameterE] : /realtime/sub_mix_db
  Parameter auto_comp(auto_comp)[N2al13ParameterBoolE] : /realtime/auto_comp
  Parameter paused(paused)[N2al13ParameterBoolE] : /realtime/paused
  Parameter elevation_mode(elevation_mode)[N2al9ParameterE] : /realtime/elevation_mode

---

[OSC] gain → 0.5
[OSC] focus → 1.5
[OSC] speaker_mix_db → 0 dB
[OSC] sub_mix_db → 0 dB
[OSC] auto_comp → off
[OSC] elevation_mode → RescaleAtmosUp (0)
[Backend] Initializing audio device...
Sample rate: 48000 Hz
Buffer size: 512 frames
Output channels: 18
Input channels: 0
[Backend] Audio device opened successfully.
Actual output channels: 18
Actual buffer size: 512
[Backend] Cached 64 source names for audio callback.
[Streaming] Background loader thread started.
[Backend] Starting audio stream...
[Backend] Audio stream started.
[Main] DBAP spatialization active: 64 sources → 16 speakers. Press Ctrl+C to stop.

[Diag] Channels — requested: 18 actual-device: 18 render-bus: 18

t=0.0s CPU=0.0% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

t=0.5s CPU=28.9% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.01s mask: 0x4 → 0x6

[RELOC-DEVICE] t=1.01s mask: 0x4 → 0x6

t=1.0s CPU=27.6% rDom=0x0 dDom=0x0 rBus=0x6 dev=0x6 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.51s mask: 0x30e0f → 0xe0f

[RELOC-DEVICE] t=1.51s mask: 0x30e0f → 0xe0f

t=1.5s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0xe0f dev=0xe0f mainRms=0.0010 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.02s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.02s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.02s dom: 0x2f2f → 0xf2f

[DOM-DEVICE] t=2.02s dom: 0x2f2f → 0xf2f

t=2.0s CPU=30.1% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.52s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.52s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.52s dom: 0x2f2f → 0xf2f

[DOM-DEVICE] t=2.52s dom: 0x2f2f → 0xf2f

t=2.5s CPU=29.4% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0036 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.02s dom: 0xf0f → 0xf2f

[DOM-DEVICE] t=3.02s dom: 0xf0f → 0xf2f

t=3.0s CPU=27.6% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.52s dom: 0xf0f → 0xf2f

[DOM-DEVICE] t=3.52s dom: 0xf0f → 0xf2f

t=3.5s CPU=29.6% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.02s dom: 0x2faf → 0xf2f

[DOM-DEVICE] t=4.02s dom: 0x2faf → 0xf2f

t=4.0s CPU=29.5% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.53s dom: 0xe0f → 0xafaf

[DOM-DEVICE] t=4.53s dom: 0xe0f → 0xafaf

t=4.5s CPU=28.5% rDom=0xafaf dDom=0xafaf rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.03s dom: 0x2faf → 0x2f2f

[DOM-DEVICE] t=5.03s dom: 0x2faf → 0x2f2f

t=5.0s CPU=27.5% rDom=0x2f2f dDom=0x2f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.54s dom: 0x2f2f → 0x2faf

[DOM-DEVICE] t=5.54s dom: 0x2f2f → 0x2faf

t=5.5s CPU=29.3% rDom=0x2faf dDom=0x2faf rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.05s dom: 0xe0f → 0xf2f

[DOM-DEVICE] t=6.05s dom: 0xe0f → 0xf2f

t=6.0s CPU=28.6% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.55s dom: 0xf2f → 0xf0f

[DOM-DEVICE] t=6.55s dom: 0xf2f → 0xf0f

t=6.5s CPU=28.8% rDom=0xf0f dDom=0xf0f rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.06s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=7.06s dom: 0x2f7f → 0xf7f

t=7.1s CPU=26.9% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.56s dom: 0xf7f → 0x2f7f

[DOM-DEVICE] t=7.56s dom: 0xf7f → 0x2f7f

t=7.6s CPU=22.9% rDom=0x2f7f dDom=0x2f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0130 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.06s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=8.06s dom: 0x2f7f → 0xf7f

t=8.1s CPU=20.1% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.57s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=8.57s dom: 0x2f7f → 0xf7f

t=8.6s CPU=28.1% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.07s dom: 0xffff → 0x7fff

[DOM-DEVICE] t=9.07s dom: 0xffff → 0x7fff

t=9.1s CPU=29.9% rDom=0x7fff dDom=0x7fff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.58s dom: 0xafff → 0x7fff

[DOM-DEVICE] t=9.58s dom: 0xafff → 0x7fff

t=9.6s CPU=30.3% rDom=0x7fff dDom=0x7fff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.08s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=10.08s dom: 0x2f7f → 0xf7f

t=10.1s CPU=22.3% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.58s dom: 0x1f7f → 0xffff

[DOM-DEVICE] t=10.58s dom: 0x1f7f → 0xffff

t=10.6s CPU=25.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.08s dom: 0x7fff → 0x7f7f

[DOM-DEVICE] t=11.08s dom: 0x7fff → 0x7f7f

t=11.1s CPU=29.9% rDom=0x7f7f dDom=0x7f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.58s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=11.58s dom: 0x7fff → 0xffff

t=11.6s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.09s dom: 0x7f7f → 0xffff

[DOM-DEVICE] t=12.09s dom: 0x7f7f → 0xffff

t=12.1s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.59s dom: 0x2f7f → 0xffff

[DOM-DEVICE] t=12.59s dom: 0x2f7f → 0xffff

t=12.6s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.09s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=13.09s dom: 0x7fff → 0xffff

t=13.1s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0130 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.60s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=13.60s dom: 0x7fff → 0xffff

t=13.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.10s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=14.10s dom: 0x7fff → 0xffff

t=14.1s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.60s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=14.60s dom: 0x7fff → 0xffff

t=14.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.1s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.1s CPU=25.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.6s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=17.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=17.6s CPU=21.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.1s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=19.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0253 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=19.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.1s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.7s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.2s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0072 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.7s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0198 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=20.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.7s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0081 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.2s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=32.2s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=32.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0347 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=33.2s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=33.7s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.2s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0220 subRms=0.0096 Xrun=0 NaN=0 SpkG=0 PLAYING

t=35.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=35.7s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.25s dom: 0xffff → 0xefef

[DOM-DEVICE] t=36.25s dom: 0xffff → 0xefef

t=36.2s CPU=29.6% rDom=0xefef dDom=0xefef rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.75s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=36.75s dom: 0xdfff → 0xffff

t=36.7s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=37.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.75s dom: 0x8f8f → 0xffff

[DOM-DEVICE] t=37.75s dom: 0x8f8f → 0xffff

t=37.7s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.26s dom: 0xefef → 0xffff

[DOM-DEVICE] t=38.26s dom: 0xefef → 0xffff

t=38.3s CPU=31.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.76s dom: 0x57df → 0xffff

[DOM-DEVICE] t=38.76s dom: 0x57df → 0xffff

t=38.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.26s dom: 0xd7ff → 0xffff

[DOM-DEVICE] t=39.26s dom: 0xd7ff → 0xffff

t=39.3s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.78s dom: 0xafff → 0xffff

[DOM-DEVICE] t=39.78s dom: 0xafff → 0xffff

t=39.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0211 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.28s dom: 0x7eff → 0xffff

[DOM-DEVICE] t=40.28s dom: 0x7eff → 0xffff

t=40.3s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=40.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=41.3s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=41.8s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=43.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=43.8s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.8s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=46.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=46.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=47.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=47.8s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=48.3s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=48.8s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=49.3s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=49.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=50.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=50.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=51.3s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=51.8s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.9s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=53.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=53.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=54.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0183 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=54.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0072 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0131 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=57.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=57.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0071 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.9s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=59.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0360 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=59.9s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=60.4s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=60.9s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=61.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=61.9s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0182 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.4s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.4s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=65.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=65.9s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=66.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.93s dom: 0xffff → 0xf9fd

[DOM-DEVICE] t=66.93s dom: 0xf9f9 → 0xffff

t=66.9s CPU=29.0% rDom=0xf9fd dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.45s dom: 0xf9fd → 0xffff

[DOM-DEVICE] t=67.45s dom: 0xf9fd → 0xffff

t=67.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.95s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=67.95s dom: 0xfdff → 0xffff

t=67.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.45s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=68.45s dom: 0xfdff → 0xffff

t=68.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0183 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.95s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=68.95s dom: 0xfdff → 0xffff

t=68.9s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.45s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=69.45s dom: 0xfdff → 0xffff

t=69.5s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.96s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=69.96s dom: 0xfdff → 0xffff

t=70.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.46s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.46s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=70.46s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=70.46s dom: 0xfdff → 0xffff

t=70.5s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.97s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.97s mask: 0xffff → 0x3ffff

t=71.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0073 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.47s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=71.47s dom: 0xfdff → 0xffff

t=71.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.97s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=71.97s dom: 0xfdff → 0xffff

t=72.0s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.48s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=72.48s dom: 0xfdfd → 0xffff

t=72.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=73.0s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.48s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=73.48s dom: 0xfdff → 0xffff

t=73.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=73.98s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=73.98s mask: 0xffff → 0x3ffff

t=74.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.50s mask: 0xffff → 0x3ffff

t=74.5s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.00s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=75.00s mask: 0x3ffff → 0xffff

t=75.0s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0134 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.50s mask: 0xffff → 0x3ffff

t=75.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.5s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=77.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=77.5s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=78.0s CPU=19.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=78.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0156 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.5s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] sub_mix_db → 0.1000 dB

t=81.0s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] sub_mix_db → -2.2000 dB

t=81.5s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=82.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

t=82.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=83.1s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=83.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=84.1s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=84.6s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.1s CPU=27.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.6s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=86.1s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=86.6s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0348 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.6s CPU=20.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=88.1s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=88.6s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=89.10s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=89.10s mask: 0xffff → 0x3ffff

t=89.1s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=89.6s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=90.1s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.60s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=90.60s dom: 0xfdff → 0xffff

t=90.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.10s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=91.10s dom: 0xfbff → 0xffff

t=91.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.61s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=91.61s dom: 0xfbff → 0xffff

t=91.6s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0207 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.12s dom: 0xf9fd → 0xffff

[DOM-DEVICE] t=92.12s dom: 0xf9fd → 0xffff

t=92.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.62s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=92.62s dom: 0xfbff → 0xffff

t=92.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=93.1s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=93.62s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=93.62s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=93.62s dom: 0xfdfd → 0xf9f9

[DOM-DEVICE] t=93.62s dom: 0xfdfd → 0xf9f9

t=93.6s CPU=28.9% rDom=0xf9f9 dDom=0xf9f9 rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.13s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.13s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.13s dom: 0xf9f9 → 0xf9fd

[DOM-DEVICE] t=94.13s dom: 0xf9f9 → 0xf9fd

t=94.1s CPU=28.4% rDom=0xf9fd dDom=0xf9fd rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.63s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.63s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.63s dom: 0xf1fb → 0xf9fb

[DOM-DEVICE] t=94.63s dom: 0xf1fb → 0xf9fb

t=94.6s CPU=29.9% rDom=0xf9fb dDom=0xf9fb rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=95.14s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=95.14s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=95.14s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=95.14s dom: 0xfbff → 0xffff

t=95.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=95.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0326 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=96.1s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0267 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.65s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=96.65s mask: 0xffff → 0x3ffff

t=96.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=97.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.66s dom: 0xdff7 → 0xffff

[DOM-DEVICE] t=97.66s dom: 0xdff7 → 0xffff

t=97.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0236 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=98.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.67s dom: 0xffdf → 0xffff

[DOM-DEVICE] t=98.67s dom: 0xffdf → 0xffff

t=98.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=99.2s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0285 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=99.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0288 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=100.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0264 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=100.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0407 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.18s dom: 0xdfdf → 0xffff

[DOM-DEVICE] t=101.18s dom: 0xdfdf → 0xffff

t=101.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0479 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=101.7s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0355 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=102.2s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0362 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.70s dom: 0xdfd7 → 0xffff

[DOM-DEVICE] t=102.70s dom: 0xdfd7 → 0xffff

t=102.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=103.2s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.70s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=103.70s dom: 0xfdfd → 0xffff

t=103.7s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=104.2s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0081 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.71s dom: 0xfdf9 → 0xffff

[DOM-DEVICE] t=104.71s dom: 0xfdf9 → 0xffff

t=104.7s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=105.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=105.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.22s dom: 0xf9f1 → 0xffff

[DOM-DEVICE] t=106.22s dom: 0xf9f1 → 0xffff

t=106.2s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=106.7s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=107.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=107.73s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=107.73s dom: 0xfffd → 0xffff

t=107.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.23s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.23s mask: 0xffff → 0x3ffff

t=108.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.74s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.74s mask: 0xffff → 0x3ffff

t=108.7s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.25s mask: 0xffff → 0x3ffff

t=109.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=109.75s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=109.75s dom: 0xfffd → 0xffff

t=109.7s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.25s mask: 0xffff → 0x3ffff

t=110.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.75s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.75s mask: 0xffff → 0x3ffff

t=110.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0221 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.25s mask: 0xffff → 0x3ffff

t=111.3s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.75s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.75s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.75s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=111.75s dom: 0xfdfd → 0xffff

t=111.8s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=112.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=112.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=113.3s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=113.8s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0111 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=114.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=114.8s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=115.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=115.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0250 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=116.3s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0206 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=116.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.8s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0206 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=118.3s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0247 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=118.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0221 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=119.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=119.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=120.3s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=120.8s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=121.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=121.8s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.3s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

t=123.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0201 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=123.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0283 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=124.3s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=124.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=125.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=125.9s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=126.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=126.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=127.4s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=127.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=128.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=128.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

t=129.4s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=129.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=130.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=130.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=131.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0169 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=131.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

t=132.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=132.9s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=133.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0192 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=133.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=134.4s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0219 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=134.9s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=135.4s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0416 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=135.9s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0356 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=136.4s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0246 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=136.9s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0246 subRms=0.0076 Xrun=0 NaN=0 SpkG=0 PLAYING

t=137.4s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0224 subRms=0.0079 Xrun=0 NaN=0 SpkG=0 PLAYING

t=137.9s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0206 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=138.4s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0217 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=138.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0238 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=139.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0257 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=140.0s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0254 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=140.5s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=141.0s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0326 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=141.5s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=142.0s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0238 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=142.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0215 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=143.0s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=143.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=144.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=144.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0221 subRms=0.0103 Xrun=0 NaN=0 SpkG=0 PLAYING

t=145.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=145.5s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0200 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=146.0s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0204 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

t=146.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0209 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=147.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=147.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0281 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=148.0s CPU=21.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0064 Xrun=0 NaN=0 SpkG=0 PLAYING

t=148.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0392 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=149.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0266 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=149.5s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0211 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=150.0s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0083 Xrun=0 NaN=0 SpkG=0 PLAYING

t=150.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0211 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=151.0s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=151.5s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0228 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=152.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=152.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=153.0s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=153.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=154.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0342 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=154.5s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0332 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=155.1s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0344 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=155.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0359 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=156.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=156.6s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=157.1s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0181 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=157.6s CPU=22.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0089 Xrun=0 NaN=0 SpkG=0 PLAYING

t=158.1s CPU=22.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=158.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=159.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=159.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=160.1s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=160.6s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=161.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=161.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=162.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=162.6s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0309 subRms=0.0072 Xrun=0 NaN=0 SpkG=0 PLAYING

t=163.1s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=163.6s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=164.1s CPU=26.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=164.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0229 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=165.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0307 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=165.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0278 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=166.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=166.62s dom: 0x8fff → 0xffff

[DOM-DEVICE] t=166.62s dom: 0x8fff → 0xffff

t=166.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0313 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=167.14s dom: 0x8fff → 0xffff

[DOM-DEVICE] t=167.14s dom: 0x8fff → 0xffff

t=167.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0379 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=167.6s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0427 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=168.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0275 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=168.64s dom: 0xbfff → 0xffff

[DOM-DEVICE] t=168.64s dom: 0xbfff → 0xffff

t=168.6s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0258 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=169.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0268 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=169.7s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=170.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0222 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=170.7s CPU=26.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=171.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=171.7s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=172.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=172.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

t=173.2s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=173.7s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0215 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=174.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=174.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0063 Xrun=0 NaN=0 SpkG=0 PLAYING

t=175.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0198 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=175.7s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0188 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=176.2s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=176.7s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0217 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=177.2s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=177.7s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0223 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=178.2s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0212 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=178.7s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0229 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=179.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0228 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=179.7s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=180.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=180.7s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0212 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=181.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0347 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=181.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0251 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=182.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=182.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0258 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=183.2s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=183.7s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0298 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=184.2s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0232 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=184.8s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=185.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=185.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=186.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0196 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=186.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=187.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=187.8s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0246 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=188.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0559 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=188.8s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0399 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=189.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0280 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=189.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0469 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=190.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0299 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=190.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0437 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=191.30s dom: 0xffff → 0xdfff

[DOM-DEVICE] t=191.30s dom: 0xffff → 0xdfff

t=191.3s CPU=28.0% rDom=0xdfff dDom=0xdfff rBus=0x3ffff dev=0x3ffff mainRms=0.0527 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=191.81s dom: 0xefff → 0xffff

[DOM-DEVICE] t=191.81s dom: 0xefff → 0xffff

t=191.8s CPU=22.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0444 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=192.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0295 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=192.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0291 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=193.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=193.8s CPU=22.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=194.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=194.83s dom: 0xf8f9 → 0xffff

[DOM-DEVICE] t=194.83s dom: 0xf8f9 → 0xffff

t=194.8s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=195.33s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=195.33s dom: 0xfdff → 0xffff

t=195.3s CPU=27.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=195.84s dom: 0xffff → 0xf9f9

[DOM-DEVICE] t=195.84s dom: 0xffff → 0xf9f9

t=195.8s CPU=29.9% rDom=0xf9f9 dDom=0xf9f9 rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=196.34s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=196.34s dom: 0xfdfd → 0xffff

t=196.3s CPU=22.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=196.8s CPU=22.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0241 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=197.3s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0289 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=197.8s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0279 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=198.4s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

t=198.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0357 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=199.4s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0315 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=199.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0334 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=200.4s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0369 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=200.9s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0642 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=201.38s dom: 0xcfdf → 0xffff

[DOM-DEVICE] t=201.38s dom: 0xcfdf → 0xffff

t=201.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0498 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=201.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0264 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=202.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0328 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=202.9s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0282 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=203.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0353 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=203.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0416 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

t=204.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0349 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=204.9s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0514 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=205.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0347 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=205.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0255 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=206.4s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0260 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=206.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0476 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=207.42s dom: 0xefff → 0xffff

[DOM-DEVICE] t=207.42s dom: 0xefff → 0xffff

t=207.4s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0363 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=207.93s dom: 0xefef → 0xffff

[DOM-DEVICE] t=207.93s dom: 0xefef → 0xffff

t=207.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0363 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=208.43s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=208.43s dom: 0xdfff → 0xffff

t=208.4s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0336 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=208.93s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=208.93s dom: 0xdfff → 0xffff

t=208.9s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0250 subRms=0.0066 Xrun=0 NaN=0 SpkG=0 PLAYING

t=209.4s CPU=25.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0359 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=209.94s dom: 0xefff → 0xffff

[DOM-DEVICE] t=209.94s dom: 0xefff → 0xffff

t=209.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0214 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

t=210.4s CPU=26.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0326 subRms=0.0078 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=210.94s dom: 0xefef → 0xffff

[DOM-DEVICE] t=210.94s dom: 0xefef → 0xffff

t=210.9s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0541 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=211.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0295 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=211.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0310 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=212.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0315 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=213.0s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=213.5s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0390 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=214.0s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0366 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=214.47s dom: 0xefff → 0xffff

[DOM-DEVICE] t=214.47s dom: 0xefff → 0xffff

t=214.5s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0423 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=215.0s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0338 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=215.5s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=216.0s CPU=22.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0242 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=216.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0349 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=217.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0290 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=217.50s dom: 0xefff → 0xffff

[DOM-DEVICE] t=217.50s dom: 0xefff → 0xffff

t=217.5s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0390 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=218.0s CPU=20.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0343 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=218.51s dom: 0xefef → 0xffff

[DOM-DEVICE] t=218.51s dom: 0xefef → 0xffff

t=218.5s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0337 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=219.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0400 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=219.5s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0372 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=220.0s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0532 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=220.52s dom: 0xcfff → 0xffff

[DOM-DEVICE] t=220.52s dom: 0xcfff → 0xffff

t=220.5s CPU=22.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0507 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=221.03s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=221.03s dom: 0xdfff → 0xffff

t=221.0s CPU=26.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0468 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=221.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0361 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=222.04s dom: 0xefff → 0xffff

[DOM-DEVICE] t=222.04s dom: 0xefff → 0xffff

t=222.0s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0271 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=222.5s CPU=22.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=223.05s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=223.05s mask: 0xffff → 0x3ffff

t=223.1s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0223 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=223.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0299 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=224.1s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0266 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=224.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0355 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=225.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0283 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=225.57s dom: 0xefff → 0xffff

[DOM-DEVICE] t=225.57s dom: 0xefff → 0xffff

t=225.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0320 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=226.1s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0226 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=226.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0178 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=227.07s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=227.07s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=227.07s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=227.07s dom: 0xdfff → 0xffff

t=227.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0222 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

t=227.6s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=228.09s dom: 0xefff → 0xffff

[DOM-DEVICE] t=228.09s dom: 0xefff → 0xffff

t=228.1s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=228.59s dom: 0xefef → 0xffff

[DOM-DEVICE] t=228.59s dom: 0xefef → 0xffff

t=228.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0266 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=229.1s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0270 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=229.59s dom: 0xcfcf → 0xffff

[DOM-DEVICE] t=229.59s dom: 0xcfcf → 0xffff

t=229.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0376 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=230.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0374 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=230.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0372 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=231.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0283 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=231.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0232 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=232.1s CPU=22.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=232.62s dom: 0xefff → 0xffff

[DOM-DEVICE] t=232.62s dom: 0xefff → 0xffff

t=232.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0278 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=233.1s CPU=22.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=233.6s CPU=25.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=234.1s CPU=25.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0296 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=234.65s dom: 0xefff → 0xffff

[DOM-DEVICE] t=234.65s dom: 0xefff → 0xffff

t=234.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0269 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=235.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0204 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=235.65s dom: 0xefef → 0xffff

[DOM-DEVICE] t=235.65s dom: 0xefef → 0xffff

t=235.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0240 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=236.16s dom: 0xefff → 0xffff

[DOM-DEVICE] t=236.16s dom: 0xefff → 0xffff

t=236.2s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0350 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=236.66s dom: 0xefef → 0xffff

[DOM-DEVICE] t=236.66s dom: 0xefef → 0xffff

t=236.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0339 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=237.16s dom: 0xefff → 0xffff

[DOM-DEVICE] t=237.16s dom: 0xefff → 0xffff

t=237.2s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0321 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=237.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0329 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=238.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0263 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=238.7s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=239.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0251 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=239.7s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=240.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=240.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=241.19s dom: 0xefef → 0xffff

[DOM-DEVICE] t=241.19s dom: 0xefef → 0xffff

t=241.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=241.7s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0205 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=242.2s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=242.70s dom: 0xefff → 0xffff

[DOM-DEVICE] t=242.70s dom: 0xefff → 0xffff

t=242.7s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0333 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

-

## TESTING ROUND 2

## Pre agent test notes 2:

Summary of all changes:

RealtimeTypes.hpp — added 8 new atomics to EngineState: renderDomMask, deviceDomMask, and their three-field relocation latches each.

Spatializer.hpp — both Phase 14 blocks rewritten:

Single pass computes per-channel mean-square into a stack-local chMs[64], tracks maxMs
Absolute mask built as before (kRmsThresh = 1e-8)
Dominant mask built in a second pass: channels with ms ≥ 0.01 × maxMs (−20 dBFS relative)
Both relocation latches add the prevMask != 0 first-block suppression guard
Fix 2 test gate left in place (isFastMover = false) — unchanged
main.cpp — monitoring loop updated:

Two new event printers: [DOM-RENDER] and [DOM-DEVICE], printed after the existing [RELOC-*] lines
Status line shows rDom and dDom before rBus/dev so the dominant cluster is the first thing visible
What to watch in the next test run:

If [RELOC-*] fires but [DOM-*] stays silent → it's far-field DBAP bleed crossing the absolute threshold, not an audible relocation
If [DOM-*] fires → the dominant speaker cluster genuinely changed, likely audible
The rDom value in the status line gives a continuous read of which cluster is carrying the energy; watch it for drift or oscillation without a [DOM-*] event

- Ascent Test 1

* not super audible channel reloc, 1 pop
* resulting output from some of it:
  t=0.0s CPU=0.0% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

t=0.5s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.01s mask: 0x4 → 0x6

[RELOC-DEVICE] t=1.01s mask: 0x4 → 0x6

t=1.0s CPU=29.9% rDom=0x0 dDom=0x0 rBus=0x6 dev=0x6 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.51s mask: 0x30e0f → 0xe0f

[RELOC-DEVICE] t=1.51s mask: 0x30e0f → 0xe0f

t=1.5s CPU=29.7% rDom=0x0 dDom=0x0 rBus=0xe0f dev=0xe0f mainRms=0.0010 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.02s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.02s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=2.02s dom: 0xf2f → 0x30f2f

t=2.0s CPU=30.1% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.52s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.52s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.52s dom: 0x32f2f → 0x30f2f

[DOM-DEVICE] t=2.52s dom: 0x32f2f → 0x30f2f

t=2.5s CPU=30.0% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0036 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=3.02s dom: 0xf2f → 0x30f2f

t=3.0s CPU=30.1% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.52s dom: 0x30f0f → 0x30f2f

[DOM-DEVICE] t=3.52s dom: 0x30f0f → 0x30f2f

t=3.5s CPU=30.1% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=4.02s dom: 0xf2f → 0x30f2f

t=4.0s CPU=29.7% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.53s dom: 0x30e0f → 0x3afaf

[DOM-DEVICE] t=4.53s dom: 0x30e0f → 0x3afaf

t=4.5s CPU=30.0% rDom=0x3afaf dDom=0x3afaf rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.03s dom: 0x32faf → 0x32f2f

[DOM-DEVICE] t=5.03s dom: 0x32faf → 0x32f2f

t=5.0s CPU=30.1% rDom=0x32f2f dDom=0x32f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.54s dom: 0x32f2f → 0x32faf

[DOM-DEVICE] t=5.54s dom: 0x32f2f → 0x32faf

t=5.5s CPU=30.1% rDom=0x32faf dDom=0x32faf rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.04s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=6.04s dom: 0x30f2f → 0xf2f

t=6.0s CPU=30.0% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.54s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=6.54s dom: 0x30f2f → 0xf2f

t=6.5s CPU=30.1% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.04s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.04s dom: 0x32f7f → 0x30f7f

t=7.0s CPU=30.1% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.55s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.55s dom: 0x32f7f → 0x30f7f

t=7.6s CPU=26.0% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.05s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=8.05s dom: 0x32f7f → 0x30f7f

t=8.1s CPU=20.1% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.55s dom: 0x30f7f → 0x32f7f

[DOM-DEVICE] t=8.55s dom: 0x30f7f → 0x32f7f

t=8.6s CPU=28.0% rDom=0x32f7f dDom=0x32f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.06s dom: 0x35f7f → 0x3ffff

[DOM-DEVICE] t=9.06s dom: 0x35f7f → 0x3ffff

t=9.1s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.56s dom: 0x3ffff → 0x30f7f

[DOM-DEVICE] t=9.56s dom: 0x3ffff → 0x30f7f

t=9.6s CPU=27.3% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.06s dom: 0xf5f → 0xf7f

[DOM-DEVICE] t=10.06s dom: 0xf5f → 0xf7f

t=10.1s CPU=28.4% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.57s dom: 0x31f7f → 0x3ffff

[DOM-DEVICE] t=10.57s dom: 0x31f7f → 0x3ffff

t=10.6s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.07s dom: 0x30f7f → 0x37fff

[DOM-DEVICE] t=11.07s dom: 0x30f7f → 0x37fff

t=11.1s CPU=28.2% rDom=0x37fff dDom=0x37fff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.57s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=11.57s dom: 0x37fff → 0x3ffff

t=11.6s CPU=28.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.07s dom: 0x37f7f → 0x3ffff

[DOM-DEVICE] t=12.07s dom: 0x37f7f → 0x3ffff

t=12.1s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.58s dom: 0x3ffff → 0x32f7f

[DOM-DEVICE] t=12.58s dom: 0x3ffff → 0x32f7f

t=12.6s CPU=28.2% rDom=0x32f7f dDom=0x32f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.08s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=13.08s dom: 0x37fff → 0x3ffff

t=13.1s CPU=29.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.59s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=13.59s dom: 0xffff → 0x3ffff

t=13.6s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.09s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=14.09s dom: 0x3ffff → 0xffff

t=14.1s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.59s dom: 0x7fff → 0x3ffff

[DOM-DEVICE] t=14.59s dom: 0x7fff → 0x3ffff

t=14.6s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.1s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=15.61s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=15.61s dom: 0xffff → 0x3ffff

t=15.6s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.1s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=16.61s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=16.61s dom: 0xffff → 0x3ffff

t=16.6s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.11s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.11s dom: 0xffff → 0x3ffff

t=17.1s CPU=30.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.62s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.62s dom: 0xffff → 0x3ffff

t=17.6s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=18.12s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=18.12s dom: 0xffff → 0x3ffff

t=18.1s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0181 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.6s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0233 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.13s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=19.13s dom: 0x3ffff → 0xffff

t=19.1s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0257 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.64s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=19.64s dom: 0xffff → 0x3ffff

t=19.6s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.1s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=20.64s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=20.64s dom: 0xffff → 0x3ffff

t=20.6s CPU=22.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.1s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=21.65s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=21.65s dom: 0xffff → 0x3ffff

t=21.7s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=22.15s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=22.15s dom: 0xffff → 0x3ffff

t=22.2s CPU=30.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=23.17s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=23.17s dom: 0xffff → 0x3ffff

t=23.2s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.7s CPU=28.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.7s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.7s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=28.70s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=28.70s dom: 0xffff → 0x3ffff

t=28.7s CPU=26.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0089 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=29.71s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=29.71s dom: 0xffff → 0x3ffff

t=29.7s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.1400

t=30.2s CPU=28.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0040 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=28.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0054 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=31.21s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=31.21s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=31.21s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=31.21s dom: 0xffff → 0x3ffff

t=31.2s CPU=28.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.1600

t=31.7s CPU=27.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.1900

[OSC] gain → 0.2100

[OSC] gain → 0.2400

[OSC] gain → 0.2500

[OSC] gain → 0.2600

[DOM-RENDER] t=32.21s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=32.21s dom: 0xffff → 0x3ffff

t=32.2s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.2800

[OSC] gain → 0.2900

[OSC] gain → 0.3000

[DOM-RENDER] t=32.73s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=32.73s dom: 0x3ffff → 0xffff

t=32.7s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=33.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.23s dom: 0xffff → 0x3ffff

t=33.2s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.2900

[OSC] gain → 0.2800

[DOM-RENDER] t=33.73s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.73s dom: 0xffff → 0x3ffff

t=33.7s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=34.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=34.23s dom: 0xffff → 0x3ffff

t=34.2s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0089 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.7s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.24s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.24s dom: 0xffff → 0x3ffff

t=35.2s CPU=29.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.74s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.74s dom: 0xffff → 0x3ffff

t=35.7s CPU=27.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.25s dom: 0x3ffff → 0x3efef

[DOM-DEVICE] t=36.25s dom: 0x3ffff → 0x3efef

t=36.2s CPU=27.8% rDom=0x3efef dDom=0x3efef rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.75s dom: 0x3dfff → 0x3ffff

[DOM-DEVICE] t=36.75s dom: 0x3dfff → 0x3ffff

t=36.7s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.26s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=37.26s dom: 0xffff → 0x3ffff

t=37.3s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.76s dom: 0x3ffff → 0x3afaf

[DOM-DEVICE] t=37.76s dom: 0x3ffff → 0x3afaf

t=37.8s CPU=20.1% rDom=0x3afaf dDom=0x3afaf rBus=0x3ffff dev=0x3ffff mainRms=0.0064 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.26s dom: 0x3efef → 0x3ffff

[DOM-DEVICE] t=38.26s dom: 0x3efef → 0x3ffff

t=38.3s CPU=24.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.77s dom: 0x357df → 0x3ffff

[DOM-DEVICE] t=38.77s dom: 0x357df → 0x3ffff

t=38.8s CPU=29.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.27s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=39.27s dom: 0xffff → 0x3ffff

t=39.3s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.78s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=39.78s dom: 0x3ffff → 0xffff

t=39.8s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.28s dom: 0xffff → 0x3ffff

t=40.3s CPU=28.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.79s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.79s dom: 0xffff → 0x3ffff

t=40.8s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.29s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.29s dom: 0xffff → 0x3ffff

t=41.3s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.79s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.79s dom: 0xffff → 0x3ffff

t=41.8s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=42.29s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=42.29s dom: 0xffff → 0x3ffff

t=42.3s CPU=27.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=43.30s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=43.30s dom: 0x3ffff → 0xffff

t=43.3s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0098 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=43.81s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=43.81s dom: 0xffff → 0x3ffff

t=43.8s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=44.81s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=44.81s dom: 0xffff → 0x3ffff

t=44.8s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=45.82s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=45.82s dom: 0xffff → 0x3ffff

t=45.8s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.33s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.33s dom: 0xffff → 0x3ffff

t=46.3s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.83s dom: 0xffff → 0x3ffff

t=46.8s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.33s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.33s dom: 0xffff → 0x3ffff

t=47.3s CPU=28.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.83s dom: 0xffff → 0x3ffff

t=47.8s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.33s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=48.33s dom: 0xffff → 0x3ffff

t=48.3s CPU=29.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.84s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=48.84s dom: 0xffff → 0x3ffff

t=48.8s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.34s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.34s dom: 0xffff → 0x3ffff

t=49.3s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.85s dom: 0xffff → 0x3ffff

t=49.8s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.35s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=50.35s dom: 0x3ffff → 0xffff

t=50.3s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=50.85s dom: 0xffff → 0x3ffff

t=50.8s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=51.35s dom: 0xffff → 0x3ffff

t=51.3s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=51.85s dom: 0xffff → 0x3ffff

t=51.9s CPU=27.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.4s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.9s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.37s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=53.37s dom: 0x3ffff → 0xffff

t=53.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=53.87s dom: 0xffff → 0x3ffff

t=53.9s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.37s dom: 0xffff → 0x3ffff

t=54.4s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.87s dom: 0xffff → 0x3ffff

t=54.9s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=55.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=55.37s dom: 0xffff → 0x3ffff

t=55.4s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=56.89s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=56.89s dom: 0xffff → 0x3ffff

t=56.9s CPU=27.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.39s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.39s dom: 0xffff → 0x3ffff

t=57.4s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.89s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.89s dom: 0xffff → 0x3ffff

t=57.9s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.40s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.40s dom: 0x3ffff → 0xffff

t=59.4s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.90s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.90s dom: 0x3ffff → 0xffff

t=59.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.41s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=60.41s dom: 0x3ffff → 0xffff

t=60.4s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.91s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=60.91s dom: 0xffff → 0x3ffff

t=60.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=61.41s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=61.41s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=61.41s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.41s dom: 0xffff → 0x3ffff

t=61.4s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=61.92s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.92s dom: 0xffff → 0x3ffff

t=61.9s CPU=27.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=62.42s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=62.42s dom: 0xffff → 0x3ffff

t=62.4s CPU=23.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.9s CPU=25.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.43s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=63.43s dom: 0xffff → 0x3ffff

t=63.4s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.94s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=63.94s dom: 0x3ffff → 0xffff

t=63.9s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=64.44s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=64.44s dom: 0xffff → 0x3ffff

t=64.4s CPU=27.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.9s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=65.45s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=65.45s dom: 0xffff → 0x3ffff

t=65.5s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=66.0s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.45s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=66.45s dom: 0xffff → 0x3ffff

t=66.5s CPU=28.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0075 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.95s dom: 0x3f9fd → 0x3ffff

[DOM-DEVICE] t=66.95s dom: 0x3f9fd → 0x3ffff

t=67.0s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=67.5s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0048 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.97s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=67.97s dom: 0x3ffff → 0xffff

t=68.0s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0044 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=68.47s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=68.47s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=68.47s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=68.47s dom: 0x3ffff → 0xffff

t=68.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.97s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=68.97s dom: 0x3fdff → 0x3ffff

t=69.0s CPU=22.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.48s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=69.48s dom: 0xffff → 0x3ffff

t=69.5s CPU=30.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.98s dom: 0xfdff → 0x3ffff

[DOM-DEVICE] t=69.98s dom: 0xfdff → 0x3ffff

t=70.0s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0071 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.49s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=70.49s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=70.49s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=70.49s dom: 0x3ffff → 0xffff

t=70.5s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0088 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.99s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.99s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=70.99s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=70.99s dom: 0xffff → 0x3ffff

t=71.0s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.50s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=71.50s dom: 0x3fdff → 0x3ffff

t=71.5s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.00s dom: 0x3ffff → 0x3fdff

[DOM-DEVICE] t=72.00s dom: 0x3ffff → 0x3fdff

t=72.0s CPU=30.1% rDom=0x3fdff dDom=0x3fdff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.50s dom: 0xfdfd → 0x3ffff

[DOM-DEVICE] t=72.50s dom: 0xfdfd → 0x3ffff

t=72.5s CPU=29.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0056 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=73.0s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0054 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.51s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=73.51s dom: 0x3ffff → 0xffff

t=73.5s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0130 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.02s mask: 0xffff → 0x3ffff

t=74.0s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.52s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=74.52s mask: 0x3ffff → 0xffff

t=74.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0071 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.02s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=75.02s mask: 0x3ffff → 0xffff

t=75.0s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0058 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.53s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.53s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=75.53s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=75.53s dom: 0xffff → 0x3ffff

t=75.5s CPU=28.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0057 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=76.03s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=76.03s dom: 0xffff → 0x3ffff

t=76.0s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.5s CPU=28.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.03s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=77.03s dom: 0xffff → 0x3ffff

t=77.0s CPU=30.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.54s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=77.54s dom: 0xffff → 0x3ffff

t=77.5s CPU=31.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0056 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=78.04s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=78.04s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=78.04s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=78.04s dom: 0x3ffff → 0xffff

t=78.0s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=78.54s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=78.54s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=78.54s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=78.54s dom: 0xffff → 0x3ffff

t=78.5s CPU=29.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.0s CPU=25.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.6s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=80.05s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=80.05s dom: 0xffff → 0x3ffff

t=80.1s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.6s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.06s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=81.06s dom: 0xffff → 0x3ffff

t=81.1s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.56s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=81.56s dom: 0x3ffff → 0xffff

t=81.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.07s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=82.07s dom: 0x3ffff → 0xffff

t=82.1s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.57s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=82.57s dom: 0xffff → 0x3ffff

t=82.6s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=83.07s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=83.07s dom: 0xffff → 0x3ffff

t=83.1s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=83.58s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=83.58s dom: 0xffff → 0x3ffff

t=83.6s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=84.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=84.09s dom: 0xffff → 0x3ffff

t=84.1s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=84.59s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=84.59s dom: 0xffff → 0x3ffff

t=84.6s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=85.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=85.09s dom: 0xffff → 0x3ffff

t=85.1s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0070 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.6s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0053 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=86.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=86.09s dom: 0xffff → 0x3ffff

t=86.1s CPU=28.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=86.60s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=86.60s dom: 0xffff → 0x3ffff

t=86.6s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.1s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.6s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=88.12s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=88.12s dom: 0x3ffff → 0xffff

t=88.1s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=88.62s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=88.62s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=88.62s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=88.62s dom: 0x3ffff → 0xffff

t=88.6s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0076 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=89.12s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=89.12s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=89.12s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=89.12s dom: 0xffff → 0x3ffff

t=89.1s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=89.6s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.13s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=90.13s dom: 0xffff → 0x3ffff

t=90.1s CPU=28.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.63s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=90.63s dom: 0x3fdff → 0x3ffff

t=90.6s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.14s dom: 0x3fbfb → 0x3f9fb

[DOM-DEVICE] t=91.14s dom: 0x3fbfb → 0x3f9fb

t=91.1s CPU=30.1% rDom=0x3f9fb dDom=0x3f9fb rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.64s dom: 0x3fbff → 0x3ffff

[DOM-DEVICE] t=91.64s dom: 0x3fbff → 0x3ffff

t=91.6s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.15s dom: 0x3f9fd → 0x3ffff

[DOM-DEVICE] t=92.15s dom: 0x3f9fd → 0x3ffff

t=92.1s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.65s dom: 0x3fbff → 0x3ffff

[DOM-DEVICE] t=92.65s dom: 0x3fbff → 0x3ffff

t=92.7s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=93.15s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=93.15s dom: 0xffff → 0x3ffff

t=93.2s CPU=28.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=93.65s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=93.65s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=93.65s dom: 0xfdfd → 0xf9f9

[DOM-DEVICE] t=93.65s dom: 0xfdfd → 0xf9f9

t=93.7s CPU=28.6% rDom=0xf9f9 dDom=0xf9f9 rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.17s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.17s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.17s dom: 0xf9f9 → 0xf9fd

[DOM-DEVICE] t=94.17s dom: 0xf9f9 → 0xf9fd

t=94.2s CPU=29.3% rDom=0xf9fd dDom=0xf9fd rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.67s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.67s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.67s dom: 0xd0f3 → 0xd0f1

[DOM-DEVICE] t=94.67s dom: 0xd0f3 → 0xd0f1

t=94.7s CPU=27.6% rDom=0xd0f1 dDom=0xd0f1 rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=95.18s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=95.18s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=95.18s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=95.18s dom: 0x3ffff → 0xffff

t=95.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=95.68s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=95.68s dom: 0x3ffff → 0xffff

t=95.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0188 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.18s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=96.18s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=96.18s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=96.18s dom: 0x3ffff → 0xffff

t=96.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0143 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.69s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=96.69s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=96.69s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=96.69s dom: 0x3ffff → 0xffff

t=96.7s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.19s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=97.19s dom: 0x3ffff → 0xffff

t=97.2s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.70s dom: 0x3dff7 → 0x3ffff

[DOM-DEVICE] t=97.70s dom: 0x3dff7 → 0x3ffff

t=97.7s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.20s dom: 0x3ffdf → 0x3ffff

[DOM-DEVICE] t=98.20s dom: 0x3ffdf → 0x3ffff

t=98.2s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0093 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.70s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=98.70s dom: 0xffff → 0x3ffff

t=98.7s CPU=27.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=99.20s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=99.20s dom: 0x3ffff → 0xffff

t=99.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=99.70s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=99.70s dom: 0xffff → 0x3ffff

t=99.7s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=100.21s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=100.21s dom: 0xffff → 0x3ffff

t=100.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=100.71s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=100.71s dom: 0xffff → 0x3ffff

t=100.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=101.23s dom: 0xffff → 0x3ffff

t=101.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0264 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.72s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=101.72s dom: 0xffff → 0x3ffff

t=101.7s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=102.23s dom: 0xffff → 0x3ffff

t=102.2s CPU=28.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.73s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=102.73s dom: 0xffff → 0x3ffff

t=102.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.24s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=103.24s dom: 0xffff → 0x3ffff

t=103.2s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.74s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=103.74s dom: 0x3ffff → 0xffff

t=103.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=104.25s dom: 0xffff → 0x3ffff

t=104.2s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=104.75s dom: 0xffff → 0x3ffff

t=104.7s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=105.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=105.25s dom: 0xffff → 0x3ffff

t=105.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=105.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=105.75s dom: 0xffff → 0x3ffff

t=105.7s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=106.25s dom: 0xffff → 0x3ffff

t=106.3s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.76s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=106.76s dom: 0xffff → 0x3ffff

t=106.8s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=107.26s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=107.26s dom: 0x3ffff → 0xffff

t=107.3s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=107.77s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=107.77s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=107.77s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=107.77s dom: 0x3ffff → 0xffff

t=107.8s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.27s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=108.27s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=108.27s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=108.27s dom: 0x3ffff → 0xffff

t=108.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0075 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.77s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.77s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=108.77s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=108.77s dom: 0xffff → 0x3ffff

t=108.8s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0064 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.28s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.28s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=109.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=109.28s dom: 0xffff → 0x3ffff

t=109.3s CPU=22.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.78s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.78s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=109.78s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=109.78s dom: 0x3ffff → 0xffff

t=109.8s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.28s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.28s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=110.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=110.28s dom: 0xffff → 0x3ffff

t=110.3s CPU=26.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=110.78s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=110.78s dom: 0xffff → 0x3ffff

t=110.8s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.30s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.30s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.30s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=111.30s dom: 0x3ffff → 0xffff

t=111.3s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.80s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.80s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=111.80s dom: 0xffff → 0x3ffff

t=111.8s CPU=27.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=112.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=112.30s dom: 0xffff → 0x3ffff

t=112.3s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=112.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=112.80s dom: 0xffff → 0x3ffff

t=112.8s CPU=28.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=113.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=113.30s dom: 0xffff → 0x3ffff

t=113.3s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=113.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=113.80s dom: 0xffff → 0x3ffff

t=113.8s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=114.31s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=114.31s dom: 0xffff → 0x3ffff

t=114.3s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0070 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=114.82s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=114.82s dom: 0xffff → 0x3ffff

t=114.8s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=115.32s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=115.32s dom: 0xffff → 0x3ffff

t=115.3s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=115.82s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=115.82s dom: 0x3ffff → 0xffff

t=115.8s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=116.32s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=116.32s dom: 0xffff → 0x3ffff

t=116.3s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=116.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=116.83s dom: 0xffff → 0x3ffff

t=116.8s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.3s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.8s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=118.3s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=118.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=118.85s dom: 0xffff → 0x3ffff

t=118.8s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=119.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=119.35s dom: 0xffff → 0x3ffff

t=119.3s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=119.9s CPU=27.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=120.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=120.35s dom: 0xffff → 0x3ffff

t=120.4s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=120.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=120.85s dom: 0xffff → 0x3ffff

t=120.9s CPU=27.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=121.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=121.35s dom: 0xffff → 0x3ffff

t=121.4s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=121.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=121.87s dom: 0xffff → 0x3ffff

t=121.9s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.4s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.9s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=123.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=123.37s dom: 0xffff → 0x3ffff

t=123.4s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

- ascent test 2

* some pops
* reloc not super audible
* some results:
  t=0.0s CPU=0.0% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

t=0.5s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.00s mask: 0x4 → 0x6

[RELOC-DEVICE] t=1.00s mask: 0x4 → 0x6

t=1.0s CPU=30.0% rDom=0x0 dDom=0x0 rBus=0x6 dev=0x6 mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.50s mask: 0x3060f → 0x30e0f

[RELOC-DEVICE] t=1.50s mask: 0x3060f → 0x30e0f

t=1.5s CPU=30.3% rDom=0x0 dDom=0x0 rBus=0x30e0f dev=0x30e0f mainRms=0.0010 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.01s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.01s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.01s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=2.01s dom: 0x30f2f → 0xf2f

t=2.0s CPU=27.9% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.51s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.51s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.51s dom: 0x32f2f → 0x30f2f

[DOM-DEVICE] t=2.51s dom: 0x32f2f → 0x30f2f

t=2.5s CPU=28.3% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0035 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=3.02s dom: 0xf2f → 0x30f2f

t=3.0s CPU=27.3% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.52s dom: 0x30f0f → 0x30f2f

[DOM-DEVICE] t=3.52s dom: 0x30f0f → 0x30f2f

t=3.5s CPU=29.6% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=4.02s dom: 0xf2f → 0x30f2f

t=4.0s CPU=27.7% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.52s dom: 0x30f2f → 0x30e0f

[DOM-DEVICE] t=4.52s dom: 0x30f2f → 0x30e0f

t=4.5s CPU=30.1% rDom=0x30e0f dDom=0x30e0f rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.03s dom: 0x32faf → 0x32f2f

[DOM-DEVICE] t=5.03s dom: 0x32faf → 0x32f2f

t=5.0s CPU=30.1% rDom=0x32f2f dDom=0x32f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.54s dom: 0x32f2f → 0x32faf

[DOM-DEVICE] t=5.54s dom: 0x32f2f → 0x32faf

t=5.5s CPU=28.8% rDom=0x32faf dDom=0x32faf rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.04s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=6.04s dom: 0x30f2f → 0xf2f

t=6.0s CPU=30.4% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.55s dom: 0xf2f → 0x30f0f

[DOM-DEVICE] t=6.55s dom: 0xf2f → 0x30f0f

t=6.5s CPU=22.6% rDom=0x30f0f dDom=0x30f0f rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.05s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.05s dom: 0x32f7f → 0x30f7f

t=7.1s CPU=30.0% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.55s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.55s dom: 0x32f7f → 0x30f7f

t=7.6s CPU=19.9% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.05s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=8.05s dom: 0x32f7f → 0x30f7f

t=8.1s CPU=30.3% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.55s dom: 0x30f7f → 0x32f7f

[DOM-DEVICE] t=8.55s dom: 0x30f7f → 0x32f7f

t=8.6s CPU=30.0% rDom=0x32f7f dDom=0x32f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.07s dom: 0x3ffff → 0x37fff

[DOM-DEVICE] t=9.07s dom: 0x3ffff → 0x37fff

t=9.1s CPU=29.8% rDom=0x37fff dDom=0x37fff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.57s dom: 0x30f7f → 0x3afff

[DOM-DEVICE] t=9.57s dom: 0x30f7f → 0x3afff

t=9.6s CPU=30.4% rDom=0x3afff dDom=0x3afff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.07s dom: 0xf7f → 0x2f7f

[DOM-DEVICE] t=10.07s dom: 0xf7f → 0x2f7f

t=10.1s CPU=29.8% rDom=0x2f7f dDom=0x2f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.57s dom: 0x31f7f → 0x3ffff

[DOM-DEVICE] t=10.57s dom: 0x31f7f → 0x3ffff

t=10.6s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.08s dom: 0x37fff → 0x37f7f

[DOM-DEVICE] t=11.08s dom: 0x37fff → 0x37f7f

t=11.1s CPU=19.8% rDom=0x37f7f dDom=0x37f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.58s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=11.58s dom: 0x37fff → 0x3ffff

t=11.6s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.09s dom: 0x37f7f → 0x3ffff

[DOM-DEVICE] t=12.09s dom: 0x37f7f → 0x3ffff

t=12.1s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.60s dom: 0x32f7f → 0x3ffff

[DOM-DEVICE] t=12.60s dom: 0x32f7f → 0x3ffff

t=12.6s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.10s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=13.10s dom: 0x37fff → 0x3ffff

t=13.1s CPU=21.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.60s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=13.60s dom: 0xffff → 0x3ffff

t=13.6s CPU=28.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.11s dom: 0xffff → 0x37fff

[DOM-DEVICE] t=14.11s dom: 0xffff → 0x37fff

t=14.1s CPU=20.0% rDom=0x37fff dDom=0x37fff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.61s dom: 0x7fff → 0x3ffff

[DOM-DEVICE] t=14.61s dom: 0x7fff → 0x3ffff

t=14.6s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.1s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=15.62s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=15.62s dom: 0xffff → 0x3ffff

t=15.6s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.1s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0192 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=16.62s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=16.62s dom: 0xffff → 0x3ffff

t=16.6s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.12s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.12s dom: 0xffff → 0x3ffff

t=17.1s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.63s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.63s dom: 0xffff → 0x3ffff

t=17.6s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=18.13s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=18.13s dom: 0xffff → 0x3ffff

t=18.1s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.6s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.15s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=19.15s dom: 0xffff → 0x3ffff

t=19.1s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0233 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.65s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=19.65s dom: 0xffff → 0x3ffff

t=19.6s CPU=22.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.1s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=20.65s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=20.65s dom: 0xffff → 0x3ffff

t=20.7s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.2s CPU=28.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=21.66s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=21.66s dom: 0xffff → 0x3ffff

t=21.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=22.17s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=22.17s dom: 0xffff → 0x3ffff

t=22.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=28.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=23.18s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=23.18s dom: 0xffff → 0x3ffff

t=23.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.7s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0156 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=29.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0073 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.7s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.7s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=28.71s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=28.71s dom: 0xffff → 0x3ffff

t=28.7s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=29.72s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=29.72s dom: 0xffff → 0x3ffff

t=29.7s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=31.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=31.23s dom: 0xffff → 0x3ffff

t=31.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0178 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.7s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=32.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=32.23s dom: 0xffff → 0x3ffff

t=32.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=32.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=32.75s dom: 0xffff → 0x3ffff

t=32.7s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0248 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=33.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.25s dom: 0xffff → 0x3ffff

t=33.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=33.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.75s dom: 0xffff → 0x3ffff

t=33.7s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=34.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=34.25s dom: 0xffff → 0x3ffff

t=34.3s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.8s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0101 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.26s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.26s dom: 0xffff → 0x3ffff

t=35.3s CPU=27.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.77s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.77s dom: 0xffff → 0x3ffff

t=35.8s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0100 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.27s dom: 0x3efef → 0x3efff

[DOM-DEVICE] t=36.27s dom: 0x3efef → 0x3efff

t=36.3s CPU=19.9% rDom=0x3efff dDom=0x3efff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.78s dom: 0x3dfff → 0x3ffff

[DOM-DEVICE] t=36.78s dom: 0x3dfff → 0x3ffff

t=36.8s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=37.28s dom: 0xffff → 0x3ffff

t=37.3s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.78s dom: 0x3afaf → 0xafaf

[DOM-DEVICE] t=37.78s dom: 0x3afaf → 0xafaf

t=37.8s CPU=19.7% rDom=0xafaf dDom=0xafaf rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.28s dom: 0x3ffff → 0x3afef

[DOM-DEVICE] t=38.28s dom: 0x3ffff → 0x3afef

t=38.3s CPU=30.2% rDom=0x3afef dDom=0x3afef rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.78s dom: 0x357df → 0x3ffff

[DOM-DEVICE] t=38.78s dom: 0x357df → 0x3ffff

t=38.8s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.29s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=39.29s dom: 0x3ffff → 0xffff

t=39.3s CPU=23.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.79s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=39.79s dom: 0x3ffff → 0xffff

t=39.8s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.30s dom: 0xffff → 0x3ffff

t=40.3s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.79s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.79s dom: 0xffff → 0x3ffff

t=40.8s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.30s dom: 0xffff → 0x3ffff

t=41.3s CPU=20.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.80s dom: 0xffff → 0x3ffff

t=41.8s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=42.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=42.30s dom: 0xffff → 0x3ffff

t=42.3s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0131 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=43.32s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=43.32s dom: 0xffff → 0x3ffff

t=43.3s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=43.8s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=44.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=44.83s dom: 0xffff → 0x3ffff

t=44.8s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=26.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=45.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=45.83s dom: 0xffff → 0x3ffff

t=45.8s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.34s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.34s dom: 0xffff → 0x3ffff

t=46.3s CPU=22.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.85s dom: 0xffff → 0x3ffff

t=46.8s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.35s dom: 0xffff → 0x3ffff

t=47.3s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.86s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.86s dom: 0xffff → 0x3ffff

t=47.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.36s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=48.36s dom: 0x3ffff → 0xffff

t=48.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.86s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=48.86s dom: 0xffff → 0x3ffff

t=48.9s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.37s dom: 0xffff → 0x3ffff

t=49.4s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.87s dom: 0xffff → 0x3ffff

t=49.9s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=50.37s dom: 0x3ffff → 0xffff

t=50.4s CPU=19.9% rDom=0x3ffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.88s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=50.88s dom: 0xffff → 0x3ffff

t=50.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.38s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=51.38s dom: 0x3ffff → 0xffff

t=51.4s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.88s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=51.88s dom: 0xffff → 0x3ffff

t=51.9s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.9s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.40s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=53.40s dom: 0xffff → 0x3ffff

t=53.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.90s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=53.90s dom: 0xffff → 0x3ffff

t=53.9s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.40s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.40s dom: 0xffff → 0x3ffff

t=54.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.90s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.90s dom: 0xffff → 0x3ffff

t=54.9s CPU=22.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=55.40s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=55.40s dom: 0xffff → 0x3ffff

t=55.4s CPU=20.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=22.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=56.92s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=56.92s dom: 0xffff → 0x3ffff

t=56.9s CPU=20.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.42s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.42s dom: 0xffff → 0x3ffff

t=57.4s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.92s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.92s dom: 0xffff → 0x3ffff

t=57.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0107 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0076 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.43s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.43s dom: 0x3ffff → 0xffff

t=59.4s CPU=25.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0356 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.94s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.94s dom: 0x3ffff → 0xffff

t=59.9s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.44s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=60.44s dom: 0x3ffff → 0xffff

t=60.4s CPU=22.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.95s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=60.95s dom: 0x3ffff → 0xffff

t=60.9s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=61.45s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.45s dom: 0xffff → 0x3ffff

t=61.5s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=61.95s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.95s dom: 0xffff → 0x3ffff

t=62.0s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.5s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.0s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.47s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=63.47s dom: 0xffff → 0x3ffff

t=63.5s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.97s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=63.97s dom: 0x3ffff → 0xffff

t=64.0s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=64.47s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=64.47s dom: 0xffff → 0x3ffff

t=64.5s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=64.97s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=64.97s dom: 0x3ffff → 0xffff

t=65.0s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0306 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=65.48s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=65.48s dom: 0xffff → 0x3ffff

t=65.5s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=66.0s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.49s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=66.49s dom: 0xffff → 0x3ffff

t=66.5s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.99s dom: 0x3f9fd → 0x3ffff

[DOM-DEVICE] t=66.99s dom: 0x3f9fd → 0x3ffff

t=67.0s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.49s dom: 0x3ffff → 0x3fdff

[DOM-DEVICE] t=67.49s dom: 0x3ffff → 0x3fdff

t=67.5s CPU=19.6% rDom=0x3fdff dDom=0x3fdff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.00s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=68.00s dom: 0x3ffff → 0xffff

t=68.0s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.50s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=68.50s dom: 0x3ffff → 0xffff

t=68.5s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.00s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=69.00s dom: 0x3fdff → 0x3ffff

t=69.0s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.51s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=69.51s dom: 0x3ffff → 0xffff

t=69.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=70.02s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=70.02s dom: 0xffff → 0x3ffff

t=70.0s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.52s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.52s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=70.52s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=70.52s dom: 0x3ffff → 0xffff

t=70.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=71.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=71.02s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=71.02s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=71.02s dom: 0xffff → 0x3ffff

t=71.0s CPU=27.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.52s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=71.52s dom: 0xffff → 0x3ffff

t=71.5s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.02s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=72.02s dom: 0x3fdff → 0x3ffff

t=72.0s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.52s dom: 0xfdfd → 0x3ffff

[DOM-DEVICE] t=72.52s dom: 0xfdfd → 0x3ffff

t=72.5s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=73.0s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.54s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=73.54s dom: 0x3ffff → 0xffff

t=73.5s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0232 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.04s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.04s mask: 0xffff → 0x3ffff

t=74.0s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.54s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.54s mask: 0xffff → 0x3ffff

t=74.5s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.04s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.04s mask: 0xffff → 0x3ffff

t=75.0s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=75.54s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=75.54s dom: 0xffff → 0x3ffff

t=75.5s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=76.05s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=76.05s dom: 0xffff → 0x3ffff

t=76.1s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.6s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.06s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=77.06s dom: 0x3ffff → 0xffff

t=77.1s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.57s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=77.57s dom: 0xffff → 0x3ffff

t=77.6s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=78.07s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=78.07s dom: 0x3ffff → 0xffff

t=78.1s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0169 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=78.57s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=78.57s dom: 0xffff → 0x3ffff

t=78.6s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.1s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=79.58s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=79.58s dom: 0x3ffff → 0xffff

t=79.6s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=80.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=80.09s dom: 0xffff → 0x3ffff

t=80.1s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.6s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.10s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=81.10s dom: 0xffff → 0x3ffff

t=81.1s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.60s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=81.60s dom: 0x3ffff → 0xffff

t=81.6s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.10s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=82.10s dom: 0xffff → 0x3ffff

t=82.1s CPU=28.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0098 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.60s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=82.60s dom: 0xffff → 0x3ffff

t=82.6s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0075 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

```

## Testing Round 1

## Pre test agent notes 1 :

Done. The (void)angleDelta suppresses any unused-variable warning since the comparison is now dead.

What to watch for in the test run:

Render-relocation and device-relocation messages should decrease or disappear if Fix 2 is the cause
Pops may also reduce, or may remain if they have a secondary source
Spatial image may be slightly less smooth for fast-moving sources (expected — sub-stepping is disabled)
Interpret the result:

Relocations gone → Fix 2 confirmed, proceed with hysteresis
Relocations remain → look elsewhere (DBAP focus changes, OutputRemap edge case, or something upstream in how poses are fed)
Relocations reduced but not eliminated → Fix 2 is a partial contributor, still need to look for another cause

- Ascent Test 1

* still resulted in reloc console messages almost immediately
* very quickly channels seemed to fully relocate

- Ascent Test 2

* still resulted in reloc console messages almost immediately
* channels didnt relocate as drastically but when they came back into focus - agressive pops and high pitched noise
```
