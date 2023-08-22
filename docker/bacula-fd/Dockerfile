FROM eftechcombr/bacula:13.0.3-base

VOLUME ["/opt/bacula/etc"]

EXPOSE 9102/tcp

ENTRYPOINT ["/opt/bacula/bin/bacula-fd"]

CMD ["-f", "-c", "/opt/bacula/etc/bacula-fd.conf"]
