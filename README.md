# darmstadt

this is an experimental Linux full-hardware screen recorder for AMD and Intel cards.

the purpose of this program is to alleviate some issues with FFmpeg when trying to record using kmsgrab and the hardware encoder (such as stutter).

## features

- uses DRM/KMS and VA-API
- very low CPU overhead
- stutter-free if your card is fast enough
- synchronizes with your display, even if you were to change the refresh rate
- records audio as well (JACK and PulseAudio supported)

# warning

no, this is not complete yet or tested on an environment that is not my Vega FE. sorry.
maybe it can work on your setup, but is very doubtful. if it does however, please tell me.

no, this won't work on NVIDIA either, of course. wait for röntgen-mode and maybe you get a chance (but I doubt because NVIDIA is so greedy with their NVFBC restrictions).
however you can use NVIDIA for encoding the AMD/Intel capture in hesse-mode. see [hesse.md](hesse.md) for more information.

the real reason why I made this public is because of a bug report I'm making in the Phoronix forums since my card began to hang again and I swear I hate it because I do not tolerate having to reboot the system whenever it doesn't feel like working. it takes like 2 minutes to start up plus I lose all my work on open applications.

you know what? I may end up switching to Intel's Xe whenever it comes out as people claim Intel is more stable in this regard... (plus they'll do 4:4:4 goodness in Ice Lake... AMD come on step up your encoding game)

# warning 2

yeah I know most of the commits contain nonsense but the truth is that this was a private project that I decided to just make public due to a bug report and since it's best reproduced with this thing I had to release it.
