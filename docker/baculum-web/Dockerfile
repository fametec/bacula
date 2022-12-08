FROM eftechcombr/bacula:13.0.1-base

RUN curl https://www.bacula.org/downloads/baculum/baculum.pub | apt-key add -

COPY baculum.list /etc/apt/sources.list.d/baculum.list

RUN apt update && \
  apt -y install \
    php-bcmath \
	php*-mbstring \
	baculum-api \
	baculum-api-apache2 \
	baculum-common \
	bacula-console \
	baculum-web \
	baculum-web-apache2

COPY sudoers-baculum /etc/sudoers.d/sudoers-baculum 

RUN usermod -aG bacula www-data && \
  chown -R www-data:bacula /opt/bacula/working /opt/bacula/etc && \
  chmod -R g+rwx /opt/bacula/working /opt/bacula/etc && \
  a2enmod rewrite && \
  a2ensite baculum-web

COPY timezone.ini /etc/php.d/timezone.ini

# COPY confs/supervisord.conf /etc/supervisord.conf

VOLUME ["/opt/bacula/etc","/etc/baculum"]

EXPOSE 9095/tcp

# RUN mkdir -p /run/php-fpm

# RUN yum -y clean all && rm -rf /var/cache/yum

# CMD ["/usr/bin/supervisord"]
