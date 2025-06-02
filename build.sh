#!/bin/bash

echo "🔧 Compilazione server..."
docker exec -w /app server gcc server.c -o server
if [ $? -eq 0 ]; then
    echo "✅ Server compilato con successo."
else
    echo "❌ Errore nella compilazione del server."
fi

echo ""

echo "🔧 Compilazione client..."
docker exec -w /app client gcc client.c -o client
if [ $? -eq 0 ]; then
    echo "✅ Client compilato con successo."
else
    echo "❌ Errore nella compilazione del client."
fi

# Simula il "pause" di Windows
read -p "Premi INVIO per continuare..."
