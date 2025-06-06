# Tris_Client_Server_LSO

## Avvio del progetto

Segui i seguenti passaggi:

- Ferma eventuali container attivi
  docker compose down

- Costruisci i container
  docker compose build

- Avvia i container in background
  docker compose up -d

- Accedi al container client
  docker exec -it client bash

- Avvia l'applicazione client all'interno del container
  ./client

## Arresto del progetto

docker compose down