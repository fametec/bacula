#!/bin/bash

su - postgres -c 'sh /scripts/make_postgresql_tables --username=bacula'

sh - postgres -c 'sh /scripts/grant_postgresql_privileges --username=bacula'


