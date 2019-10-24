# Bacula 9.4.4 Container

Deploy the bacula community edition on Docker Containers. 

## Images

- [x] Bacula Catalog fametec/bacula-catalog
- [x] Bacula Director fametec/bacula-director
- [x] Bacula Storage Daemon fametec/bacula-storage
- [x] Bacula File Daemon fametec/bacula-client
- [x] Baculum Web Gui 

## Install Docker 

    curl -sSL https://get.docker.com | bash
    
## Install Docker-compose

    curl -L "https://github.com/docker/compose/releases/download/1.24.1/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    chmod +x /usr/local/bin/docker-compose

## Download and Install Bacula Container

    git clone https://github.com/fametec/bacula
    cd bacula/docker
    docker-compose up

## Tests

    docker exec -it docker_bacula-dir_1 bash
    > bconsole
    * 
    
    

## Docker Compose

docker-compose.yaml


    version: '3.1'
    services:
      db:
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
      bacula-dir:
        image: fametec/bacula-director:latest
        restart: unless-stopped
        volumes:
          - ./etc/bacula-dir.conf:/opt/bacula/etc/bacula-dir.conf:ro
          - ./etc/bconsole.conf:/opt/bacula/etc/bconsole.conf:ro
        depends_on:
          - db
        ports:
          - 9101
      bacula-sd:
        image: fametec/bacula-storage:latest
        restart: unless-stopped
        depends_on:
          - bacula-dir
          - db
        volumes:
          - ./etc/bacula-sd.conf:/opt/bacula/etc/bacula-sd.conf:ro
        ports:
          - 9103
      bacula-fd:
        image: fametec/bacula-client:latest
        restart: unless-stopped
        depends_on:
          - bacula-sd
          - bacula-dir
        volumes:
          - ./etc/bacula-fd.conf:/opt/bacula/etc/bacula-fd.conf:ro
        ports:
          - 9102
    volumes:
      pgdata:

## Support

For technical support please contact us. 

suporte@fametec.com.br

## e-Learning 

https://www.fametec.com.br


## Reference

http://www.bacula.lat/community/baculum/ 

http://www.bacula.lat/community/script-instalacao-bacula-community-9-x-pacotes-oficiais/

https://www.bacula.org/documentation/documentation/
