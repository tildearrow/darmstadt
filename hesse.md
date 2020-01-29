# hesse

this is the AMD/Intel+NVIDIA combo screen capture mode.

# information

the hesse mode of darmstadt allows you to capture your AMD/Intel card and send the picture to the NVIDIA card for encoding.

# this makes no sense. what's the point?

the point is that NVIDIA is the only to have implemented YUV 4:4:4 encoding in their cards.

this chroma subsampling scheme delivers better picture quality by not having to mess around with the colors.
therefore the resulting video looks almost identical. I will put some samples later.

however, it is less supported by hardware decoders as it is a format mostly designed for professional usage.

# usage

```
./darmstadt -hesse ...
```

for normal mode.

```
./darmstadt -hesse -absoluteperfection ...
```

for 4:4:4 (super high quality) mode.

# note

using hesse mode involves additional CPU/GPU overhead to transfer the picture between the AMD/Intel and NVIDIA cards.
fast RAM recommended for higher resolutions.

# why "hesse"?

because this was the original codename for a future AMD/Intel+NVIDIA combo recorder, but instead of splitting it into separate projects I decided to make it a single one.
the hesse bits weren't that hard to implement after all.

# ok, seriously, why "hesse"?

you're curious, aren't ya? :p

because:

1. Hassium is before Darmstadtium in the periodic table.
2. Hesse is where Darmstadt is located at.

# ok, now why "darmstadt"?

...

you sure want to know everything.

because:

1. it contains the letters "D", "R" and "M". it uses DRM/KMS to capture the screen (no, no, not Digital Rights Management but Direct Rendering Manager).
2. it just sounded harsh/strong to me.
