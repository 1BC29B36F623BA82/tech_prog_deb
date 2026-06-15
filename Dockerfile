FROM debian:trixie

LABEL maintainer="Dev Team <dev@example.com>"

RUN apt-get update 
RUN apt-get install -y qtbase5-dev g++ make

COPY . /opt/some_app

WORKDIR /opt/some_app

RUN qmake echoServer.pro && make && mv echoServer server_app

EXPOSE 8080

CMD ["/opt/some_app/server_app"]
