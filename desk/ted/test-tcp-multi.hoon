:: test-tcp-multi: open two sequential connections, verify both work
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
  |=  [wir=wire got=?]
  =/  m  (strand ,?)
  ^-  form:m
  ;<  gif=gift:tcp  bind:m  (tcp-fact wir)
  ?:  ?=(%receive -.gif)
    ~&  ">> [{<wir>}] received {<p.data.gif>} bytes"
    (tcp-read-all wir &)
  ?.  ?=(%closed -.gif)
    (strand-fail:strandio %unexpected-gift ~[leaf+"got {<-.gif>}"])
  ~&  ">> [{<wir>}] closed"
  (pure:m got)
--
^-  thread:spider
|=  arg=vase
=/  m  (strand ,vase)
^-  form:m
;<  =bowl:spider  bind:m  get-bowl:strandio
=/  req
  %^  cat  3
    'GET / HTTP/1.0\0d\0aHost: example.com\0d\0a'
  'Connection: close\0d\0a\0d\0a'
:: connection /a
~&  ">> [/a] connecting..."
;<  ~  bind:m  (watch-our:strandio /a %tcp /a)
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%connect /a [%.y %turf ~[['com' 'example' ~]] 443]])]
;<  gif-a=gift:tcp  bind:m  (tcp-fact /a)
~&  ">> [/a] got {<-.gif-a>}"
?>  =(-.gif-a %connected)
~&  ">> [/a] sending HTTP GET"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%send /a [(met 3 req) req]])]
;<  got-a=?  bind:m  (tcp-read-all /a |)
?>  got-a
~&  ">> [/a] done"
:: connection /b
~&  ">> [/b] connecting..."
;<  ~  bind:m  (watch-our:strandio /b %tcp /b)
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%connect /b [%.y %turf ~[['com' 'example' ~]] 443]])]
;<  gif-b=gift:tcp  bind:m  (tcp-fact /b)
~&  ">> [/b] got {<-.gif-b>}"
?>  =(-.gif-b %connected)
~&  ">> [/b] sending HTTP GET"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%send /b [(met 3 req) req]])]
;<  got-b=?  bind:m  (tcp-read-all /b |)
?>  got-b
~&  ">> [/b] done"
(pure:m !>('test-tcp-multi: both connections ok'))
