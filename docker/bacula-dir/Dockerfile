FROM fametec/bacula-base:11.0.5

MAINTAINER eduardo@fametec.com.br

VOLUME ["/opt/bacula/etc"]

EXPOSE 9101/tcp

ENTRYPOINT ["bacula-dir"]

CMD ["-f", "-c", "/opt/bacula/etc/bacula-dir.conf"]
