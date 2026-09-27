# The host side of the Linux build: conquertron's recomp and the generated C.
# Fedora with a compiler and Capstone 5.0.3 (PowerPC only), built once and
# cached by podman. Used by tools/linux-all.sh, so an immutable desktop
# (Bluefin, Silverblue) needs nothing installed on the host.
FROM registry.fedoraproject.org/fedora:41
RUN dnf install -y -q cmake gcc-c++ make zlib-devel git python3 && dnf clean all
RUN git clone -q --depth 1 --branch 5.0.3 https://github.com/capstone-engine/capstone.git /tmp/capstone \
 && cmake -S /tmp/capstone -B /tmp/capstone/build -DCMAKE_BUILD_TYPE=Release \
      -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF -DCAPSTONE_PPC_SUPPORT=ON \
      -DCAPSTONE_BUILD_TESTS=OFF -DCAPSTONE_BUILD_CSTOOL=OFF -DCMAKE_INSTALL_PREFIX=/opt/capstone \
 && cmake --build /tmp/capstone/build -j"$(nproc)" && cmake --install /tmp/capstone/build \
 && rm -rf /tmp/capstone
