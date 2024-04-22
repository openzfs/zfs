FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get update

RUN apt-get install -y \
      debhelper-compat \
      devscripts

ENV WORK_DIR /zfs_app/zfs
WORKDIR ${WORK_DIR}

ADD . ${WORK_DIR}/

RUN mk-build-deps --build-dep contrib/debian/control
RUN apt install -y ./*.deb
RUN sh autogen.sh
RUN ./configure
RUN cp -a contrib/debian debian
RUN sed 's/@CFGOPTS@/--enable-debuginfo/g' debian/rules.in > debian/rules
RUN chmod +x debian/rules
RUN dch -b -M --force-distribution --distribution bullseye-truenas-unstable "Tagged from ixsystems/zfs CI"
RUN debuild -us -uc -b
RUN rm ../openzfs-zfs-dracut_*.deb
RUN rm ../openzfs-zfs-initramfs_*.deb
RUN apt-get install -y ../*.deb
