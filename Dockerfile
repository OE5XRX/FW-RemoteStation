FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
                        build-essential \
                        ninja-build \
                        clang-format \
                        cmake \
                        gcc-arm-none-eabi \
                        gdb \
                        git \
                        doxygen \
                        graphviz \
                        python3-pip \
                        pkg-config \
                        cpputest \
                        cppcheck
RUN pip install --break-system-packages cmakelang Jinja2
