FROM fametec/bacula-base:9.6.3

MAINTAINER eduardo@fametec.com.br

VOLUME ["/opt/bacula/etc","/etc/baculum"]

RUN yum install -y bacula-cloud-storage --exclude=bacula-mysql

RUN for i in `ls /opt/bacula/bin`; do if test -z /usr/sbin/$i; then ln -s /opt/bacula/bin/$i /usr/sbin/$i; fi; done 

RUN rpm --import http://bacula.org/downloads/baculum/baculum.pub

COPY confs/baculum.repo /etc/yum.repos.d/baculum.repo

RUN yum install -y epel-release yum-utils

RUN yum install -y http://rpms.remirepo.net/enterprise/remi-release-7.rpm

RUN yum-config-manager --enable remi-php73

RUN yum install -y \
	sudo \
	baculum-common \
	baculum-api \
	baculum-api-httpd \
	baculum-web \
	baculum-web-httpd

COPY confs/sudoers-baculum /etc/sudoers.d/sudoers-baculum 

COPY confs/Config-api-apache /etc/baculum/Config-api-apache

COPY confs/Config-web-apache /etc/baculum/Config-web-apache

COPY confs/timezone.ini /etc/php.d/99-timezone.ini

RUN chown -R apache:bacula /opt/bacula/working /opt/bacula/etc \
  && chmod -R g+rwx /opt/bacula/working /opt/bacula/etc \
  && chmod -R o+w /etc/baculum/Config-api-apache \
  && chmod -R o+w /etc/baculum/Config-web-apache

RUN yum -y install python-setuptools && easy_install supervisor

ADD confs/supervisord.conf /etc/supervisord.conf

RUN yum -y clean all && rm -rf /var/cache/yum

EXPOSE 9101/tcp 9102/tcp 9103/tcp 9095/tcp 9096/tcp
 
CMD ["/usr/bin/supervisord"]