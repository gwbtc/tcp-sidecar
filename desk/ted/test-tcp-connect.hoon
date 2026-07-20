:: test-tcp-connect: connect to a host, wait for %connected, then close
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
--
^-  thread:spider
|=  arg=vase
=/  m  (strand ,vase)
^-  form:m
;<  =bowl:spider  bind:m  get-bowl:strandio
~&  ">> subscribing to /test"
;<  ~  bind:m  (watch-our:strandio /test %tcp /test)
~&  ">> connecting to example.com:443 (tls)"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%connect /test [%.y %turf ~[['com' 'example' ~]] 443] ~])]
;<  gif=gift:tcp  bind:m  (tcp-fact /test)
~&  ">> got {<-.gif>}"
?>  =(-.gif %connected)
~&  ">> sending %close"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%close /test])]
;<  gif2=gift:tcp  bind:m  (tcp-fact /test)
~&  ">> got {<-.gif2>}"
?>  =(-.gif2 %closed)
(pure:m !>('test-tcp-connect: ok'))
