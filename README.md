# tcp-sidecar

TCP client for Urbit via Lick IPC. A stepping stone toward a kernel TCP vane.

## What it does

Gives Urbit agents outbound TCP connections with TLS support. The `%tcp` agent exposes a standard Gall subscription interface — agents poke to send commands and subscribe to receive events. The C sidecar handles the actual socket IO.

## API

### Tasks (agent pokes)

```hoon
:tcp &tcp-task [%connect /wire [secure=? =fief]]
:tcp &tcp-task [%send /wire data=@]
:tcp &tcp-task [%close /wire]
```

### Gifts (subscription facts)

```hoon
[%connected =wire]
[%receive =wire data=@]
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

## Example: agent usage

```hoon
:: Subscribe to connection events
[%pass /my-conn %agent [our.bowl %tcp] %watch /my-conn]

:: Open a TLS connection
[%pass /my-conn %agent [our.bowl %tcp] %poke %tcp-task !>([%connect /my-conn [%.y %turf ~[['com' 'example' ~]] 443]])]

:: Send data
[%pass /my-conn %agent [our.bowl %tcp] %poke %tcp-task !>([%send /my-conn 'hello'])]

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
