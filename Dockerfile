FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y \
        build-essential \
        bsdmainutils \
        libreadline-dev \
        libsdl2-dev \
        libglew-dev \
        libcurl4-openssl-dev

WORKDIR /sm64