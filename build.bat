@echo off
echo 🔧 Compilazione server...
docker exec -w /app server gcc server.c -o server
IF %ERRORLEVEL% EQU 0 (
    echo ✅ Server compilato con successo.
) ELSE (
    echo ❌ Errore nella compilazione del server.
)

echo.

echo 🔧 Compilazione client...
docker exec -w /app client gcc client.c -o client
IF %ERRORLEVEL% EQU 0 (
    echo ✅ Client compilato con successo.
) ELSE (
    echo ❌ Errore nella compilazione del client.
)

pause
