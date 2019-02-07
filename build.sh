#!/bin/sh

apt-get install \
    cmake \
    g++ \
    libasound2-dev \
    libcurl4-gnutls-dev \
    libgcrypt20-dev \
    libgsm1-dev \
    libopus-dev \
    libpopt-dev \
    libsigc++-2.0-dev \
    libspeex-dev \
    libdbus-1-dev \
    libjsoncpp-dev \
    tcl-dev

mkdir -p src/build && cd src/build

cmake -DCMAKE_INSTALL_PREFIX=/opt/SVXLink \
      -DSYSCONF_INSTALL_DIR=/opt/SVXLink/etc \
      -DLOCAL_STATE_DIR=/opt/SVXLink/var \
      -DCMAKE_INSTALL_RPATH=/opt/SVXLink/lib \
      -DINTERNAL_SAMPLE_RATE=8000 \
      -DUSE_GPROF=NO \
      -DUSE_QT=NO \
      -DUSE_OSS=NO \
      -DCPACK_DEBIAN_PACKAGE_SHLIBDEPS=YES \
      -DCPACK_DEBIAN_PACKAGE_CONTROL_EXTRA="../../debian/conffiles;../../debian/postrm;../../debian/preinst" \
      ..

make -j16
cpack -G DEB
