:: test-tcp-stress: repeated connections to trigger segfault
::
/-  spider
/-  tcp
/+  strandio
=,  strand=strand:spider
=>
|%
++  tcp-fact
  |=  wir=wire
  =/  m  (strand ,gift:tcp)
  ^-  form:m
  ;<  =cage  bind:m  (take-fact:strandio wir)
  (pure:m !<(gift:tcp q.cage))
::
++  tcp-read-all
  |=  wir=wire
  =/  m  (strand ,~)
  ^-  form:m
  ;<  gif=gift:tcp  bind:m  (tcp-fact wir)
  ?:  ?=(%receive -.gif)
    (tcp-read-all wir)
  (pure:m ~)
::
++  do-one
  |=  n=@ud
  =/  m  (strand ,~)
  ^-  form:m
  =/  wir  /stress/(scot %ud n)
  ~&  ">> [{<n>}] connecting..."
  ;<  ~  bind:m  (watch-our:strandio wir %tcp wir)
  ;<  ~  bind:m
    %-  poke-our:strandio
    [%tcp %tcp-task !>(`task:tcp`[%connect wir [%.y %turf ~[['com' 'example' ~]] 443] ~])]
  ;<  gif=gift:tcp  bind:m  (tcp-fact wir)
  ?.  ?=(%connected -.gif)
    ~&  ">> [{<n>}] unexpected: {<-.gif>}"
    (pure:m ~)
  =/  req
    %^  cat  3
      'GET / HTTP/1.0\0d\0aHost: example.com\0d\0a'
    'Connection: close\0d\0a\0d\0a'
  ;<  ~  bind:m
    %-  poke-our:strandio
    [%tcp %tcp-task !>(`task:tcp`[%send wir [(met 3 req) req]])]
  ;<  ~  bind:m  (tcp-read-all wir)
  ~&  ">> [{<n>}] done"
  (pure:m ~)
::
++  do-loop
  |=  [n=@ud max=@ud]
  =/  m  (strand ,~)
  ^-  form:m
  ?:  =(n max)
    ~&  ">> stress test complete: {<max>} connections"
    (pure:m ~)
  ;<  ~  bind:m  (do-one n)
  (do-loop +(n) max)
--
^-  thread:spider
|=  arg=vase
=/  m  (strand ,vase)
^-  form:m
;<  ~  bind:m  (do-loop 0 50)
(pure:m !>('stress test passed'))
