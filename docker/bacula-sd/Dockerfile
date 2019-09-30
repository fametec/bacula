FROM debian:10.1-slim AS base

MAINTAINER eduardo@fametec.com.br

ENV EMAIL suporte@fametec.com.br

ENV HOST localhost

RUN apt-get update && apt-get install -y --no-install-recommends \
	libreadline7 \
	zlib1g \
	liblzo2-2 \
	mt-st \
	mtx \
	# postfix \
	libacl1 \
	libssl1.1 \
	postgresql \
&& rm -rf /var/lib/apt/lists/*

FROM base AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential \
	libreadline6-dev \
	zlib1g-dev \
	liblzo2-dev \
	mt-st \
	mtx \
	# postfix \
	libacl1-dev \
	libssl-dev \
	postgresql-server-dev-all \
	postgresql \
&& rm -rf /var/lib/apt/lists/*

COPY bacula-9.4.4 /usr/src/bacula

RUN cd /usr/src/bacula \
&& PREFIX=/opt/bacula \
&& CFLAGS="-g -O2 -Wall" \
&& ./configure \
    --with-readline=/usr/include/readline \
    --sbindir=${PREFIX}/bin \
    --sysconfdir=/etc/bacula \
    --docdir=${PREFIX}/html \
    --htmldir=${PREFIX}/html \
    --with-working-dir=${PREFIX}/working \
    --with-pid-dir=${PREFIX}/working \
    --with-scriptdir=${PREFIX}/scripts \
    --with-plugindir=${PREFIX}/plugins \
    --libdir=${PREFIX}/lib \
    --enable-smartalloc \
    --disable-conio \
    --disable-bat \
    --with-postgresql \
    --with-archivedir=/mnt/backup \
    --with-hostname=$HOST \
    --with-dump-email=$EMAIL \
    --with-job-email=$EMAIL \
    --with-smtp-host=$SMTP \
    --with-dir-password=DIR_P4ssw0rd \
    --with-sd-password=SD_P4ssw0rd \
    --disable-build-dird \
&& make -j8 \
&& make install

FROM base

COPY --from=build /opt/bacula /opt/bacula

COPY bacula-sd.conf /etc/bacula/

VOLUME ["/mnt/backup","/etc/bacula"]

EXPOSE 9103/tcp

ENTRYPOINT ["/opt/bacula/bin/bacula-sd"]

CMD ["-fc","/etc/bacula/bacula-sd.conf"]

