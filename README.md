# tcp-sidecar

TCP client for Urbit via Lick IPC. A stepping stone toward a kernel TCP vane.

## What it does

Gives Urbit agents outbound TCP connections with TLS support. The `%tcp` agent exposes a standard Gall subscription interface — agents poke to send commands and subscribe to receive events. The C sidecar handles the actual socket IO.

## API

### Tasks (agent pokes)

```hoon
:tcp &tcp-task [%connect /wire [secure=? =fief]]
:tcp &tcp-task [%send /wire data=octs]
:tcp &tcp-task [%close /wire]
```

### Gifts (subscription facts)

```hoon
[%connected =wire]
[%receive =wire data=octs]
[%closed =wire]
[%error =wire msg=@t]
```

### Addressing (`fief`)

```hoon
[%turf (list turf) port=@ud]   :: domain name
[%if @if port=@ud]              :: IPv4
[%is @is port=@ud]              :: IPv6
```

## Setup

```sh
# 1. Build the sidecar
cd sidecar
make

# 2. Configure sync target
cp config.example.json config.json
# Edit config.json with your pier path

# 3. Sync desk to ship
./sync.sh
# Or manually: rsync -av --delete desk/ /path/to/pier/tcp-sidecar/

# 4. On your ship
|new-desk %tcp-sidecar
|mount %tcp-sidecar
# (sync/rsync files into the mounted desk)
|commit %tcp-sidecar
|install our %tcp-sidecar

# 5. Start the sidecar
./sidecar/tcp-sidecar /path/to/pier
```

## Repo structure

```
desk/                    Urbit desk
  sur/tcp.hoon           Type definitions (fief, target, task, gift)
  app/tcp.hoon           Gall agent — Lick bridge with subscriptions
  mar/tcp-task.hoon      Mark for agent pokes
  mar/tcp-gift.hoon      Mark for sidecar gifts
  ted/                   Test threads
    test-tcp-connect     Lifecycle: connect, close
    test-tcp-get         HTTP GET over TLS
    test-tcp-multi       Sequential connections
    test-tcp-mux         Multiplexed concurrent connections
    test-tcp-connect-v6  IPv6 connect via localhost
    test-tcp-get-v6      IPv6 send/receive via localhost

sidecar/                 C sidecar
  main.c                 Poll loop, connection management, noun helpers
  ur/                    Vere's noun library (hashcons, jam/cue)
  Makefile
```

## Dependencies

- OpenSSL (for TLS)
- Vere with Lick support

## Testing

With the sidecar running:

```
-tcp-sidecar!test-tcp-connect
-tcp-sidecar!test-tcp-get
-tcp-sidecar!test-tcp-multi
-tcp-sidecar!test-tcp-mux
```

### IPv6 tests

The IPv6 tests use the loopback address `::1` instead of a remote host.
Most networks don't have IPv6 internet connectivity — your machine supports
IPv6 locally, but routing to external IPv6 addresses requires your ISP to
assign a prefix and your router to advertise it. Using localhost sidesteps
this while still exercising the full `%is` code path: `@is` atom parsing,
byte ordering in the sidecar, `getaddrinfo` with an IPv6 address, and
`connect()` over an `AF_INET6` socket.

Start a local TCP listener first, then run the thread:

```sh
# Terminal 1: listen on IPv6 localhost
nc -l6 12345
```

```
:: Terminal 2: dojo
-tcp-sidecar!test-tcp-connect-v6
```

For the send/receive test, start `nc -l6 12345` again, run
`-tcp-sidecar!test-tcp-get-v6`, then type a response in nc and ctrl-c
to close.

## Example: agent usage

```hoon
:: Subscribe to connection events
[%pass /my-conn %agent [our.bowl %tcp] %watch /my-conn]

:: Open a TLS connection
[%pass /my-conn %agent [our.bowl %tcp] %poke %tcp-task !>([%connect /my-conn [%.y %turf ~[['com' 'example' ~]] 443]])]

:: Send data
[%pass /my-conn %agent [our.bowl %tcp] %poke %tcp-task !>([%send /my-conn 5 'hello'])]

:: Handle events in on-agent
++  on-agent
  |=  [=wire =sign:agent:gall]
  ?+  -.sign  (on-agent:def wire sign)
      %fact
    =/  gif  !<(gift:tcp q.cage.sign)
    ?-  -.gif
      %connected  ...
      %receive    ...
      %closed     ...
      %error      ...
    ==
  ==
```
