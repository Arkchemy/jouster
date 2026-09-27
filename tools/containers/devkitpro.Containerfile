# The Switch side of the Linux build: devkitA64, libnx and deko3d from
# devkitPro's own image, plus the six portlibs jouster links (the same list
# tools/setup-windows.ps1 installs -- the base image does not carry them).
FROM docker.io/devkitpro/devkita64
RUN dkp-pacman -Sy --noconfirm --needed \
      switch-bzip2 switch-curl switch-dav1d switch-ffmpeg switch-mbedtls switch-zlib \
 && dkp-pacman -Scc --noconfirm
