FROM fametec/bacula-base

MAINTAINER eduardo@fametec.com.br

RUN rpm --import http://bacula.org/downloads/baculum/baculum.pub

COPY baculum.repo /etc/yum.repos.d/baculum.repo

RUN yum install -y \
	sudo \
	baculum-common \
	baculum-api \
	baculum-api-httpd \
	baculum-web \
	baculum-web-httpd

RUN yum -y clean all && rm -rf /var/cache/yum

COPY sudoers-baculum /etc/sudoers.d/sudoers-baculum 

RUN { \
        echo '[Date]'; \
        echo 'date.timezone = UTC ; '; \
  } > /etc/php.d/timezone.ini


VOLUME ["/opt/bacula/etc","/etc/baculum"]

RUN chown -R apache:bacula /opt/bacula/working /opt/bacula/etc \
  && chmod -R g+rwx /opt/bacula/working /opt/bacula/etc \
  && chmod -R o+w /etc/baculum/Config-api-apache \
  && chmod -R o+w /etc/baculum/Config-web-apache

EXPOSE 9095/tcp 9096/tcp

# ENTRYPOINT [""]

CMD ["httpd", "-D", "FOREGROUND"]

