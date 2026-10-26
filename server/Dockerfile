FROM golang:1.25-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /cartarchy-server .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /cartarchy-server /cartarchy-server
VOLUME ["/data"]
EXPOSE 8080
ENTRYPOINT ["/cartarchy-server", "-data", "/data/cartarchy.sqlite"]
