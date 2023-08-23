FROM eftechcombr/bacula:13.0.3-base

VOLUME ["/opt/bacula/etc"]

EXPOSE 9101/tcp

ENTRYPOINT ["/opt/bacula/bin/bacula-dir"]

CMD ["-f", "-c", "/opt/bacula/etc/bacula-dir.conf"]
