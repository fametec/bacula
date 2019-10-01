# Bacula Container

Deploy the bacula community edition on Docker Containers. 

## Workloads

- [x] Bacula Catalog
- [x] Bacula Director
- [x] Bacula Storage Daemon
- [x] Bacula File Daemon
- [ ] Baculum Web Gui

## Docker Compose

- [x] docker-compose.yaml


version: '3.1'
#
services:

  base:
    build: bacula-base/
    image: fametec/bacula-base:latest
#
  db:
    build: bacula-catalog/
    image: fametec/bacula-catalog:latest
    restart: unless-stopped
    environment:
      POSTGRES_PASSWORD: bacula
      POSTGRES_USER: bacula
      POSTGRES_DB: bacula
    volumes:
    - pgdata:/var/lib/postgresql/data:rw
    ports:
      - 5432
#
  bacula-dir:
    build: bacula-dir/
    image: fametec/bacula-director:latest
    restart: unless-stopped
    volumes:
      - ./etc/bacula-dir.conf:/opt/bacula/etc/bacula-dir.conf:ro
      - ./etc/bconsole.conf:/opt/bacula/etc/bconsole.conf:ro
    depends_on:
      - db
    ports:
      - 9101
#
  bacula-sd:
    build: bacula-sd/
    image: fametec/bacula-storage:latest
    restart: unless-stopped
    depends_on:
      - bacula-dir
      - db
    volumes:
      - ./etc/bacula-sd.conf:/opt/bacula/etc/bacula-sd.conf:ro
    ports:
      - 9103
#
  bacula-fd:
    build: bacula-fd/
    image: fametec/bacula-client:latest
    restart: unless-stopped
    depends_on:
      - bacula-sd
      - bacula-dir
    volumes:
      - ./etc/bacula-fd.conf:/opt/bacula/etc/bacula-fd.conf:ro
    ports:
      - 9102
#
volumes:
  pgdata:

## Support

For technical support please contact us. 

suporte@fametec.com.br

## e-Learning 

https://www.fametec.com.br


