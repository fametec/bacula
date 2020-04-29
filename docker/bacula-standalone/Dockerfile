FROM fametec/bacula-base:9.6.3

MAINTAINER eduardo@fametec.com.br

RUN yum install -y bacula-cloud-storage --exclude=bacula-mysql

RUN for i in `ls /opt/bacula/bin`; do if test -z /usr/sbin/$i; then ln -s /opt/bacula/bin/$i /usr/sbin/$i; fi; done 

RUN yum -y install python-setuptools && easy_install supervisor

ADD scripts/supervisord.conf /etc/supervisord.conf

VOLUME ["/opt/bacula/etc"]

EXPOSE 9101/tcp 9102/tcp 9103/tcp  
 
CMD ["/usr/bin/supervisord"]