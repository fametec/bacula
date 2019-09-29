FROM debian

MAINTAINER eduardo@fametec.com.br

ENV EMAIL suporte@fametec.com.br

ENV HOST localhost

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

RUN cd /usr/src/bacula && ./configure \
	--with-readline=/usr/include/readline \
	--disable-conio \
	--bindir=/usr/bin \
	--sbindir=/usr/sbin \
	--with-scriptdir=/etc/bacula/scripts \
	--with-working-dir=/var/lib/bacula \
	--with-logdir=/var/log \
	--enable-smartalloc \
	--with-postgresql \
	--with-archivedir=/mnt/backup \
	--with-job-email=$EMAIL \
	--with-hostname=$HOST \
	--with-dir-password=Dir_P4ssw0rd \
        --with-sd-password=SD_P4ssw0rd \
        --disable-build-stored \
&& make -j8 && make install 

COPY bacula-dir.conf /etc/bacula/

EXPOSE 9101/tcp

ENTRYPOINT ["bacula-dir"]

CMD ["-fc","/etc/bacula/bacula-dir.conf"]

# CMD [ "sleep","1000" ]	
