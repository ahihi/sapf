################################# 
FROM ubuntu AS base

RUN apt-get update
RUN apt-get install -y libedit-dev libfftw3-dev librtaudio-dev libsndfile1-dev

################################# 
FROM base AS build

RUN apt-get install -y git pkg-config meson cmake nix-bin

ADD . /src
WORKDIR /src

RUN nix --extra-experimental-features 'nix-command flakes' develop

RUN meson setup --buildtype release build
RUN meson compile -C build
RUN meson test --verbose -C build

################################# 
FROM base AS sapf 

COPY --from=build /src/build/sapf /usr/bin/sapf

CMD ["/usr/bin/sapf"]
