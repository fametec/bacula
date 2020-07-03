FROM fametec/bacula-base:9.6.5

MAINTAINER eduardo@fametec.com.br

RUN rpm --import http://bacula.org/downloads/baculum/baculum.pub

COPY baculum.repo /etc/yum.repos.d/baculum.repo

# RUN dnf install -y \
#	epel-release \
#        yum-utils

# RUN dnf install -y http://rpms.remirepo.net/enterprise/remi-release-7.rpm

# RUN dnf-config-manager --enable remi-php73

RUN dnf install -y \
	sudo \
	baculum-common \
	baculum-api \
	baculum-api-httpd \
	baculum-web \
	baculum-web-httpd

RUN dnf -y clean all && rm -rf /var/cache/yum

COPY sudoers-baculum /etc/sudoers.d/sudoers-baculum 

COPY timezone.ini /etc/php.d/timezone.ini


VOLUME ["/opt/bacula/etc","/etc/baculum"]

RUN chown -R apache:bacula /opt/bacula/working /opt/bacula/etc \
  && chmod -R g+rwx /opt/bacula/working /opt/bacula/etc \
  && chmod -R o+w /etc/baculum/Config-api-apache \
  && chmod -R o+w /etc/baculum/Config-web-apache

EXPOSE 9095/tcp 9096/tcp

RUN dnf -y install epel-release 

RUN dnf -y install supervisor
# python3-setuptools && easy_install supervisor

ADD confs/supervisord.conf /etc/supervisord.conf

RUN mkdir -p /run/php-fpm
# ENTRYPOINT [""]

# CMD ["httpd", "-D", "FOREGROUND"]

CMD ["/usr/bin/supervisord"]