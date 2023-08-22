FROM debian:11

ENV BACULA_VERSION 13.0.3

ENV DISTRO bullseye

ENV DEBIAN_FRONTEND noninteractive

# get your key on: https://www.bacula.org/bacula-binary-package-download/
ENV BACULA_KEY 5cee4d079821e

ENV EMAIL suporte@eftech.com.br

RUN apt update && \
  apt -y install gnupg2 curl && \
  curl https://bacula.org/downloads/Bacula-4096-Distribution-Verification-key.asc | apt-key add - && \
  echo "deb https://www.bacula.org/packages/${BACULA_KEY}/debs/${BACULA_VERSION} ${DISTRO} main" > /etc/apt/sources.list.d/bacula-community.list && \
  apt update && \
  apt -y install dbconfig-pgsql bacula-postgresql

RUN for i in `ls /opt/bacula/bin`; do if test -z /usr/sbin/$i; then ln -s /opt/bacula/bin/$i /usr/sbin/$i; fi; done 