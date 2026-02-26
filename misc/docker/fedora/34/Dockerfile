FROM fedora:34
ARG test
RUN yum install -y git gcc make
RUN mkdir -p /usr/src
RUN git clone https://github.com/namhyung/motrace /usr/src/motrace
RUN if [ "$test" = "yes" ] ; then \
        cd /usr/src/motrace \
        && ./misc/install-deps.sh -y \
        && ./configure && make ASAN=1 && make ASAN=1 unittest; \
    else \
        cd /usr/src/motrace && ./misc/install-deps.sh -y && ./configure && make && make install; \
    fi
