FROM fametec/bacula-base:11.0.2

MAINTAINER eduardo@fametec.com.br

RUN rpm --import http://bacula.org/downloads/baculum/baculum.pub

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
	baculum-api \
	baculum-api-httpd \
	baculum-web \
	baculum-web-httpd \
	supervisor

COPY sudoers-baculum /etc/sudoers.d/sudoers-baculum 

COPY timezone.ini /etc/php.d/timezone.ini

COPY confs/supervisord.conf /etc/supervisord.conf

VOLUME ["/opt/bacula/etc","/etc/baculum"]

RUN chown -R apache:bacula /opt/bacula/working /opt/bacula/etc \
  && chmod -R g+rwx /opt/bacula/working /opt/bacula/etc \
  && chmod -R o+w /etc/baculum/Config-api-apache \
  && chmod -R o+w /etc/baculum/Config-web-apache

EXPOSE 9095/tcp 9096/tcp

RUN mkdir -p /run/php-fpm

RUN yum -y clean all && rm -rf /var/cache/yum

CMD ["/usr/bin/supervisord"]
