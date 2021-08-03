FROM fametec/bacula-base:11.0.5 AS base

FROM postgres:10

MAINTAINER eduardo@fametec.com.br

ENV POSTGRES_PASSWORD bacula

ENV POSTGRES_USER bacula

ENV POSTGRES_DB bacula

ENV POSTGRES_INITDB_ARGS '--encoding=SQL_ASCII --lc-collate=C --lc-ctype=C'

COPY --from=base /opt/bacula/scripts/make_postgresql_tables /docker-entrypoint-initdb.d/make_postgresql_tables

COPY --from=base /opt/bacula/scripts/grant_postgresql_privileges /docker-entrypoint-initdb.d/grant_postgresql_privileges

RUN { \
	echo '#!/bin/bash'; \
	echo 'sh /docker-entrypoint-initdb.d/make_postgresql_tables --username=$POSTGRES_USER'; \
	echo 'sh /docker-entrypoint-initdb.d/grant_postgresql_privileges --username=$POSTGRES_USER'; \
} >> /docker-entrypoint-initdb.d/deploy_database.sh \
&& chmod +x /docker-entrypoint-initdb.d/deploy_database.sh \
&& chown postgres. /docker-entrypoint-initdb.d/deploy_database.sh

# COPY 01-make_postgresql_tables.sql 02-grant_postgresql_privileges.sql /docker-entrypoint-initdb.d/

RUN chown -R postgres. /docker-entrypoint-initdb.d/*

VOLUME ["/var/lib/postgresql/data"]

EXPOSE 5432/tcp

