Sizigy
========

Sizigy is a pet project born as a way to renew a bit of knowledge of the low
level programming in C.  It's an MQTT message broker built upon epoll
interface, aiming to guarantee *exactly once* semantic.  Distribution is still
in development as well as a lot of the core features, but it is already
possible to play with it. It's a good exercise to explore and better understand
the TCP stack, endianness, serialization and scalability.

## Quickstart

TBD.

## Under the hood

Basically it's an epoll server, when the system starts the main thread create
an epoll file descriptor and share it among a pool of workers, EPOLLONESHOT
flag allow the wake up of just one thread per event, and the kernel takes all
the responsibility of events handling.  For the distribution part it creates a
separate epoll instance, used by a dedicated thread which manage the
communication with other peers.
Each thread handle incoming data from a connection by using a dedicated *ring
buffer*, this way it is possible to avoid loss of data by the MTU limit in case
of huge data payloads.

## Protocol

TBD.

## Changelog

See the [CHANGELOG](CHANGELOG) file.
