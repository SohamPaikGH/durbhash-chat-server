FROM alpine:latest AS builder
RUN apk add --no-cache gcc g++ musl-dev make
WORKDIR /build
COPY . .
RUN make

FROM scratch
WORKDIR /app
COPY --from=builder /build/dbs_server .
COPY --from=builder /build/dbs_client .
CMD ["./dbs_server"]
