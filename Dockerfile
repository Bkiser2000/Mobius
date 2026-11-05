FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    gcc make libssl-dev git pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Headers are now self-contained in ./include/
RUN make clean && make all

# Runtime stage
FROM alpine:latest

RUN apk add --no-cache libssl3 libc6-compat

WORKDIR /app
COPY --from=builder /build/mobius_pl /build/mobius_score ./

ENV MOBIUS_AUDIT_MAX_BYTES=5242880
EXPOSE 8080

ENTRYPOINT ["./mobius_score"]