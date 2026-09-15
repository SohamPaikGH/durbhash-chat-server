FROM --platform=linux/amd64 ubuntu:latest AS builder
RUN apt-get update
RUN apt-get install -y gcc g++ make
WORKDIR /build
COPY . .
RUN make

FROM --platform=linux/amd64 ubuntu:latest
WORKDIR /app
COPY --from=builder /build/dbs-server .
COPY --from=builder /build/dbs-client .
CMD ["./dbs-server"]
