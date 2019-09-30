FROM debian:10.1-slim AS base

MAINTAINER eduardo@fametec.com.br

ENV VERSION 9.4.4

ENV EMAIL suporte@fametec.com.br

ENV HOST localhost

ENV SMTP smtp

RUN apt-get update && apt-get install -y --no-install-recommends \
	libreadline7 \
	zlib1g \
	liblzo2-2 \
	libssl1.1 \
	mt-st \
	mtx \
	libacl1 \
	postgresql \
&& rm -rf /var/lib/apt/lists/*

FROM base AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential \
	libreadline-dev \
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

ADD https://ufpr.dl.sourceforge.net/project/bacula/bacula/$VERSION/bacula-$VERSION.tar.gz /tmp/bacula.tar.gz

RUN tar -zxf /tmp/bacula.tar.gz -C /usr/src/bacula

# COPY bacula-9.4.4 /usr/src/bacula

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
    --with-hostname=$HOST \
    --with-dump-email=$EMAIL \
    --with-job-email=$EMAIL \
    --with-smtp-host=$SMTP \
    --with-dir-password=DIR_P4ssw0rd \
    --with-sd-password=SD_P4ssw0rd \
    --disable-build-stored \
    --with-baseport=9101 \
&& make -j8 \
&& make install 

FROM base

COPY --from=build /opt/bacula /opt/bacula

COPY bacula-dir.conf bconsole.conf conf.d /etc/bacula/

VOLUME ["/etc/bacula"]

EXPOSE 9101/tcp

ENTRYPOINT ["/opt/bacula/bin/bacula-dir"]

CMD ["-f", "-c", "/etc/bacula/bacula-dir.conf"]

