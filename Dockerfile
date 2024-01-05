FROM debian:bullseye-slim

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update

RUN apt-get install -y \
      debhelper-compat \
      devscripts

ENV WORK_DIR /zfs_app/zfs
WORKDIR ${WORK_DIR}

ADD . ${WORK_DIR}/

RUN cp -a contrib/truenas debian
RUN mk-build-deps --build-dep
RUN apt install -y ./*.deb
RUN dch -b -M --force-distribution --distribution bullseye-truenas-unstable "Tagged from ixsystems/zfs CI"
RUN debuild -us -uc -b
RUN apt-get install -y ../*.deb
