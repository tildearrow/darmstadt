# not yet

the -h264 and -hevc options do nothing at the moment. the program only uses HEVC for now.

# operator's manual

## prerequisites/recommendations

- set your card to the highest memory clock possible.
  - this ensures the hardware encoder will guarantee a constant encode time.
- ensure the program has the `CAP_SYS_ADMIN` capability set.
  - if this is not the case, the program will prompt you to do so.
- ensure your card is able to record at your current resolution/frame-rate in real-time.
  - this program doesn't offer cropping/scaling yet. sorry.

## notes

- as stated above, this program isn't capable of cropping or scaling yet.
- no, there is no frame-rate control. it's all synchronized to the monitor refresh rate.

## usage

```
./darmstadt [-h264|-hevc] [file]
```

as soon as you run the program, it will begin recording. press `Ctrl-C` to stop.

if no file is specified, the program will output to out.ts.

by default, darmstadt encodes in H.264 (AVC) for resolutions lower than 2880x1800 and HEVC (H.265) otherwise.


