FROM gcc:latest

WORKDIR /app

COPY server.c .
COPY server.h .

RUN gcc server.c -o server

CMD ["./server"]
