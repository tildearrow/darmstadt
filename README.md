# darmstadt

this is an experimental Linux full-hardware screen recorder for AMD and Intel cards.

the purpose of this program is to alleviate some issues with FFmpeg when trying to record using kmsgrab and the hardware encoder (such as stutter and lack of cursor). somebody finally [reported](https://trac.ffmpeg.org/ticket/8377) the issue ~9 months after this project's initial private release.

## features

- uses DRM/KMS, EGL and VA-API
- very low CPU overhead
- stutter-free if your card is fast enough
- synchronizes with your display, even if you were to change the refresh rate
- records audio as well (JACK and PulseAudio supported)
- optionally allows you to encode with an NVIDIA card using darmstadt's Hesse mode
- optionally allows you to encode in software using darmstadt's Meitner mode

# warning

**recording at 1080p in AMD cards outputs at 1920x1088 for some weird reason. I will fix this.**

**it is best you record in .nut format. .mkv may work, but as of FFmpeg 4.3, it apparently does not.**

works on my machine...
maybe it can work on your setup, but is very doubtful. if it does however, please tell me.

might work on Intel, but I don't know.

this won't work on NVIDIA, of course. wait for röntgen-mode and maybe you get a chance (but I doubt because NVIDIA is so greedy with their NVFBC restrictions).
however you can use NVIDIA for encoding the AMD/Intel capture in hesse mode. see [hesse.md](hesse.md) for more information.

# warning 2

yeah I know most of the commits contain nonsense but the truth is that this was a private project that I decided to just make public due to a bug report and since it's best reproduced with this thing I had to release it.
