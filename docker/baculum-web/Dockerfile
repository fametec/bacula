FROM fametec/bacula-base:11.0.5

MAINTAINER eduardo@fametec.com.br

RUN rpm --import https://www.bacula.org/downloads/baculum/baculum.pub

COPY baculum.repo /etc/yum.repos.d/baculum.repo

RUN yum install -y \
	epel-release \
        yum-utils

RUN yum install -y http://rpms.remirepo.net/enterprise/remi-release-7.rpm

RUN yum-config-manager --enable remi-php73

RUN yum install -y \
	sudo \
	php-fpm \
	baculum-common \
	baculum-web \
	baculum-web-httpd \
	supervisor

COPY sudoers-baculum /etc/sudoers.d/sudoers-baculum 

COPY timezone.ini /etc/php.d/timezone.ini

COPY confs/supervisord.conf /etc/supervisord.conf

VOLUME ["/opt/bacula/etc","/etc/baculum"]

EXPOSE 9095/tcp

RUN mkdir -p /run/php-fpm

RUN yum -y clean all && rm -rf /var/cache/yum

CMD ["/usr/bin/supervisord"]
