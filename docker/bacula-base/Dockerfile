FROM centos:7

MAINTAINER Eduardo Fraga <eduardo.fraga@fametec.com.br>

ENV BACULA_VERSION 9.4.4

ENV BACULA_KEY 5cee4d079821e

ENV EMAIL suporte@fametec.com.br

RUN rpm --import https://www.bacula.org/downloads/Bacula-4096-Distribution-Verification-key.asc

COPY bacula-community.repo /etc/yum.repos.d/bacula-community.repo

RUN yum -y install \
	zip \
	bzip2

RUN yum install -y bacula-postgresql --exclude=bacula-mysql

RUN yum -y clean all && rm -rf /var/cache/yum

RUN for i in `ls /opt/bacula/bin`; do ln -s /opt/bacula/bin/$i /usr/sbin/$i; done 

#ENTRYPOINT ["bacula-dir"]

#CMD ["-f", "-c", "/etc/bacula/bacula-dir.conf"]
