FROM ghcr.io/zephyrproject-rtos/zephyr-build:main

# Default powerline10k theme, no plugins installed
RUN sh -c "$(wget -O- https://github.com/deluan/zsh-in-docker/releases/download/v1.2.1/zsh-in-docker.sh)"

RUN sudo apt-get update && sudo apt-get install -y \
    minicom \
    picocom \
    screen \
    dfu-util \
    clang-format-18 \
    vim \
    && sudo apt-get clean && sudo rm -rf /var/lib/apt/lists/*

RUN sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 180
