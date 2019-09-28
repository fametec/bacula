FROM postgres

MAINTAINER eduardo@fametec.com.br

ENV POSTGRES_PASSWORD bacula

ENV POSTGRES_USER bacula

ENV POSTGRES_DB bacula

ENV POSTGRES_INITDB_ARGS '--encoding=SQL_ASCII --lc-collate=C --lc-ctype=C'

COPY scripts /scripts

RUN chown -R postgres. /scripts

VOLUME ["/var/lib/postgresql/data"]

EXPOSE 5432/tcp



