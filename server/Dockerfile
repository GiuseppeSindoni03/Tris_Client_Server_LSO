FROM gcc:latest

WORKDIR /app

# Copia sia server.c che server.h
COPY server.c .
COPY server.h .

RUN gcc server.c -o server

CMD ["./server"]
