FROM centos:8.2.2004

MAINTAINER Eduardo Fraga <eduardo.fraga@fametec.com.br>

ENV BACULA_VERSION 9.6.5

# get your key on: https://www.bacula.org/bacula-binary-package-download/
ENV BACULA_KEY 5cee4d079821e

ENV EMAIL suporte@fametec.com.br

RUN rpm --import https://www.bacula.org/downloads/Bacula-4096-Distribution-Verification-key.asc

COPY bacula-community.repo /etc/yum.repos.d/bacula-community.repo

RUN sed -i s/BACULA_VERSION/$(echo $BACULA_VERSION)/g /etc/yum.repos.d/bacula-community.repo \
  && sed -i s/BACULA_KEY/$(echo $BACULA_KEY)/g /etc/yum.repos.d/bacula-community.repo

#RUN rpm -i https://download.postgresql.org/pub/repos/yum/reporpms/EL-8-x86_64/pgdg-redhat-repo-latest.noarch.rpm

#RUN dnf -y upgrade

#RUN dnf -y install postgresql \
RUN dnf -y install \
	postgresql \
	zip \
	bzip2 \
	mtx 

RUN dnf install -y bacula-postgresql-$BACULA_VERSION

RUN dnf -y clean all && rm -rf /var/cache/yum

RUN for i in `ls /opt/bacula/bin`; do ln -s /opt/bacula/bin/$i /usr/sbin/$i; done 

