FROM alpine:3.14
ARG test
RUN apk update
RUN apk add build-base linux-headers git bash libunwind-dev
RUN mkdir -p /usr/src
RUN git clone https://github.com/namhyung/motrace /usr/src/motrace
RUN if [ "${test}" == "yes" ] ; then \
        cd /usr/src/motrace && ./misc/install-deps.sh && ./configure && make && make unittest; \
    else \
        cd /usr/src/motrace && ./misc/install-deps.sh && ./configure && make && make install; \
    fi
