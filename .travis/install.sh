#! /usr/bin/env bash

if [[ "$TRAVIS_OS_NAME" == "linux" ]]; then
  if [ -d ~/ffmpeg/ffmpeg-$FFMPEG_VERSION ]; then
    cd ~/ffmpeg/ffmpeg-$FFMPEG_VERSION
  else
    mkdir -p ~/ffmpeg
    cd ~/ffmpeg
    wget https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.bz2
    tar jxf ffmpeg-$FFMPEG_VERSION.tar.bz2
    cd ffmpeg-$FFMPEG_VERSION
    PATH="$HOME/bin:$PATH" ./configure --prefix="$HOME/ffmpeg/ffmpeg-$FFMPEG_VERSION-build" --enable-shared --disable-x86asm
    PATH="$HOME/bin:$PATH" make -s -j
  fi

  make install
  export PATH="$PATH:$HOME/ffmpeg/ffmpeg-$FFMPEG_VERSION-build/bin"
  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$HOME/ffmpeg/ffmpeg-$FFMPEG_VERSION-build/lib"
  export C_INCLUDE_PATH="$C_INCLUDE_PATH:$HOME/ffmpeg/ffmpeg-$FFMPEG_VERSION-build/include"
  hash -r
  cd $TRAVIS_BUILD_DIR
elif [[ "$TRAVIS_OS_NAME" == "osx" ]]; then
  brew install ffmpeg --without-gpl --without-x264 --without-xvid
else
  echo "Unknown operating system '$TRAVIS_OS_NAME'"
  exit 1
fi