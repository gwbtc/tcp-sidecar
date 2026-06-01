:: test-tcp-mux: two concurrent connections, multiplexed reads
::
/-  spider
/-  tcp
/+  strandio
=,  strand=strand:spider
=>
|%
+$  mux-state
  $:  a-data=?
      a-done=?
      b-data=?
      b-done=?
  ==
::
++  tcp-fact-prefix
  |=  pfx=path
  =/  m  (strand ,[path gift:tcp])
  ^-  form:m
  ;<  [pax=path cag=cage]  bind:m  (take-fact-prefix:strandio pfx)
  (pure:m [pax !<(gift:tcp q.cag)])
::
++  mux-loop
  |=  st=mux-state
  =/  m  (strand ,mux-state)
  ^-  form:m
  ?:  &(a-done.st b-done.st)
    (pure:m st)
  ;<  [wir=path gif=gift:tcp]  bind:m  (tcp-fact-prefix /conn)
  =/  who=tape  (spud wir)
  =/  is-a  =(wir /conn/a)
  ?+    -.gif
    (strand-fail:strandio %unexpected-gift ~[leaf+"[{who}] got {<-.gif>}"])
  ::
      %receive
    ~&  ">> [{who}] received {<p.data.gif>} bytes"
    ?:  is-a
      (mux-loop st(a-data &))
    (mux-loop st(b-data &))
  ::
      %closed
    ~&  ">> [{who}] closed"
    ?:  is-a
      (mux-loop st(a-done &))
    (mux-loop st(b-done &))
  ==
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
:: subscribe both with shared prefix /conn
~&  ">> subscribing /conn/a and /conn/b"
;<  ~  bind:m  (watch-our:strandio /conn/a %tcp /a)
;<  ~  bind:m  (watch-our:strandio /conn/b %tcp /b)
:: connect both
~&  ">> connecting /a and /b to example.com:443"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%connect /a [%.y %turf ~[['com' 'example' ~]] 443]])]
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%connect /b [%.y %turf ~[['com' 'example' ~]] 443]])]
:: wait for both connected (order doesn't matter)
~&  ">> waiting for both connected..."
;<  [pax=path gif=gift:tcp]  bind:m  (tcp-fact-prefix /conn)
~&  ">> [{(spud pax)}] got {<-.gif>}"
?>  =(-.gif %connected)
;<  [pax2=path gif2=gift:tcp]  bind:m  (tcp-fact-prefix /conn)
~&  ">> [{(spud pax2)}] got {<-.gif2>}"
?>  =(-.gif2 %connected)
:: send on both
~&  ">> sending HTTP GET on both"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%send /a [(met 3 req) req]])]
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%send /b [(met 3 req) req]])]
:: multiplex reads until both closed
~&  ">> multiplexing reads..."
;<  st=mux-state  bind:m  (mux-loop *mux-state)
?>  a-data.st
?>  b-data.st
~&  ">> both connections got data and closed"
(pure:m !>('test-tcp-mux: ok'))
