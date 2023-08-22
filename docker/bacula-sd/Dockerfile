FROM eftechcombr/bacula:13.0.3-base

RUN apt -y install bacula-cloud-storage-common bacula-cloud-storage-s3 bacula-aligned

VOLUME ["/opt/bacula/etc"]

EXPOSE 9103/tcp

ENTRYPOINT ["/opt/bacula/bin/bacula-sd"]

CMD ["-f", "-c", "/opt/bacula/etc/bacula-sd.conf"]
