FROM fametec/bacula-base:11.0.2

VOLUME ["/opt/bacula/etc"]

EXPOSE 9102/tcp

ENTRYPOINT ["bacula-fd"]

CMD ["-f", "-c", "/opt/bacula/etc/bacula-fd.conf"]
