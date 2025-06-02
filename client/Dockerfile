FROM gcc:latest

WORKDIR /app

COPY client.c .

RUN gcc client.c -o client

CMD ["tail", "-f", "/dev/null"]

