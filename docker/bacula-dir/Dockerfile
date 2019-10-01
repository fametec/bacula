FROM fametec/bacula-base

MAINTAINER eduardo@fametec.com.br

VOLUME ["/etc/bacula/etc"]

EXPOSE 9101/tcp

ENTRYPOINT ["bacula-dir"]

CMD ["-f", "-c", "/opt/bacula/etc/bacula-dir.conf"]
