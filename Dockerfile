FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y \
        build-essential \
        bsdmainutils \
        libreadline-dev \
        libsdl2-dev \
        libglew-dev \
        libcurl4-openssl-dev

RUN mkdir /sm64
WORKDIR /sm64
ENV PATH="/sm64/tools:${PATH}"

# docker build -t sm64coopdx .
# docker run --rm --mount type=bind,source="$(pwd)",destination=/sm64 sm64coopdx make -j HEADLESS=1 
# see https://github.com/n64decomp/sm64/blob/master/README.md for advanced usage

# update this file later
