FROM postgres

MAINTAINER eduardo@fametec.com.br

ENV POSTGRES_PASSWORD bacula

ENV POSTGRES_USER bacula

ENV POSTGRES_DB bacula

ENV POSTGRES_INITDB_ARGS '--encoding=SQL_ASCII --lc-collate=C --lc-ctype=C'

COPY 01-make_postgresql_tables.sql 02-grant_postgresql_privileges.sql /docker-entrypoint-initdb.d/

RUN chown -R postgres. /docker-entrypoint-initdb.d/*

VOLUME ["/var/lib/postgresql/data"]

EXPOSE 5432/tcp

