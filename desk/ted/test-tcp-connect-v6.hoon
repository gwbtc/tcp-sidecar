:: test-tcp-connect-v6: connect to localhost via ipv6, wait for %connected,
:: then close.  run `nc -l6 12345` before starting this thread.
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
~&  ">> subscribing to /test-v6"
;<  ~  bind:m  (watch-our:strandio /test-v6 %tcp /test-v6)
~&  ">> connecting to ::1 port 12345 (plain)"
;<  ~  bind:m
  %-  poke-our:strandio
  :*  %tcp  %tcp-task
    !>  ^-  task:tcp
    [%connect /test-v6 [%.n %is .0.0.0.0.0.0.0.1 12.345]]
  ==
;<  gif=gift:tcp  bind:m  (tcp-fact /test-v6)
~&  ">> got {<-.gif>}"
?>  =(-.gif %connected)
~&  ">> sending %close"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%close /test-v6])]
;<  gif2=gift:tcp  bind:m  (tcp-fact /test-v6)
~&  ">> got {<-.gif2>}"
?>  =(-.gif2 %closed)
(pure:m !>('test-tcp-connect-v6: ok'))
