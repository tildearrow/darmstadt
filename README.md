# darmstadt

this is an experimental full-hardware screen recorder for VA-API compatible cards (Intel and AMD).

the purpose of this program is to alleviate some issues with FFmpeg when trying to record using kmsgrab and the hardware encoder (such as stutter).

## features

- uses DRM/KMS and VA-API
- very low CPU overhead
- stutter-free if your card is fast enough
- synchronizes with your display, even if you were to change the refresh rate

# warning

no, this is not complete yet or tested on an environment that is not my Vega FE. sorry.
maybe it can work on your setup, but is very doubtful. if it does however, please tell me.

no, this won't work on Intel because they want you to use /dev/dri/render instead of just /dev/dri/card. so i'm sorry, but no.

no, this won't work on NVIDIA either, of course. wait for röntgen and maybe you get a chance.

the real reason why I made this public is because of a bug report I'm making in the Phoronix forums since my card began to hang again and I swear I hate it because I do not tolerate having to reboot the system whenever it doesn't feel like working. it takes like 2 minutes to start up plus I lose all my work on open applications.

you know what? I may end up switching to Intel's Xe whenever it comes out as people claim Intel is more stable in this regard... (plus they'll do 4:4:4 goodness in Ice Lake... AMD come on step up your encoding game)
