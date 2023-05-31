# releases

## v1.0

- initial version

## v1.1

- adds 1ms of delay to vblank interval to achieve frame perfection

## v1.2

- write cache for even more perfect frames

# roadmap

## v2.0

- even more frame perfection
- command line options
- should work on intel
- audio recording
- public release

## v3.0

- use OpenGL for compositing multiple planes (and therefore have a working cursor)

## v4.0

- encoding and capturing are now 2 separate threads for even more perfect frames than before

## v5.0

- AMF backend for AMD cards?
  - this will allow us to achieve true frame perfection by being able to use a lower encoding profile
- Intel Ice Lake 4:4:4 support?
  - by this time we'll try to switch to an Intel dedicated card and be done with this for once
- röntgen: NVIDIA capture support using NvFBC?
  - Quadro/Tesla-only perhaps
