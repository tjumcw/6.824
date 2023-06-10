FROM ubuntu:18.04
WORKDIR /
RUN apt-get update && \
    apt-get install -y g++ git wget libssl-dev make

RUN wget https://cmake.org/files/v3.22/cmake-3.22.1.tar.gz && \
    tar -xvzf cmake-3.22.1.tar.gz && cd cmake-3.22.1 && \
    chmod 777 ./configure && ./configure && make -j4 && make -j4 install

RUN cd lib && git clone https://github.com/zeromq/libzmq.git && \
    cd libzmq && mkdir build && cd build && cmake .. && make -j4 install && \
    cd /lib && git clone https://github.com/zeromq/cppzmq.git && \
    cd cppzmq && mkdir build && cd build && cmake .. && make -j4 install

RUN cp /usr/local/lib/libzmq.so.5 /lib && rm cmake-3.22.1.tar.gz

# 宿主机运行命令
# git clone https://github.com/tjumcw/6.824.git
# docker run -it --name=6.824 -v /Users/miaochangwei1/Desktop/6.824:/6.824 6.824