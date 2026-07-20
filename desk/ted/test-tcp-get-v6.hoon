:: test-tcp-get-v6: send data over ipv6 and receive a response.
:: run `nc -l6 12345` before starting this thread, then type a
:: response in the nc terminal and ctrl-c to close.
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
++  tcp-collect
  |=  [wir=wire body=@t n=@ud]
  =/  m  (strand ,@t)
  ^-  form:m
  ;<  gif=gift:tcp  bind:m  (tcp-fact wir)
  ?+    -.gif
    (strand-fail:strandio %unexpected-gift ~[leaf+"got {<-.gif>}"])
      %receive
    ~&  ">> chunk {<+(n)>}: {<p.data.gif>} bytes"
    (tcp-collect wir (cat 3 body (,@t q.data.gif)) +(n))
  ::
      %closed
    ~&  ">> connection closed after {<n>} chunks"
    (pure:m body)
  ==
--
^-  thread:spider
|=  arg=vase
=/  m  (strand ,vase)
^-  form:m
;<  =bowl:spider  bind:m  get-bowl:strandio
~&  ">> subscribing to /get-v6"
;<  ~  bind:m  (watch-our:strandio /get-v6 %tcp /get-v6)
~&  ">> connecting to ::1 port 12345 (plain)"
;<  ~  bind:m
  %-  poke-our:strandio
  :*  %tcp  %tcp-task
    !>  ^-  task:tcp
    [%connect /get-v6 [%.n %is .0.0.0.0.0.0.0.1 12.345] ~]
  ==
;<  gif=gift:tcp  bind:m  (tcp-fact /get-v6)
~&  ">> got {<-.gif>}"
?>  =(-.gif %connected)
=/  req  'hello from urbit over ipv6!\0a'
~&  ">> sending {<(met 3 req)>} bytes"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%send /get-v6 [(met 3 req) req]])]
~&  ">> waiting for response (type in nc and ctrl-c)..."
;<  body=@t  bind:m  (tcp-collect /get-v6 '' 0)
=/  len  (met 3 body)
~&  ">> received: {(trip body)}"
%-  pure:m
!>((crip "{(scow %ud len)} bytes received over ipv6"))
