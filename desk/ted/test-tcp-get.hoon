:: test-tcp-get: HTTP GET over TLS, print response
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
    ~&  ">> chunk {<+(n)>}: {<(met 3 data.gif)>} bytes"
    (tcp-collect wir (cat 3 body (,@t data.gif)) +(n))
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
~&  ">> subscribing to /get"
;<  ~  bind:m  (watch-our:strandio /get %tcp /get)
~&  ">> connecting to example.com:443 (tls)"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%connect /get [%.y %turf ~[['com' 'example' ~]] 443]])]
;<  gif=gift:tcp  bind:m  (tcp-fact /get)
~&  ">> got {<-.gif>}"
?>  =(-.gif %connected)
=/  req
  %^  cat  3
    'GET / HTTP/1.0\0d\0aHost: example.com\0d\0a'
  'Connection: close\0d\0a\0d\0a'
~&  ">> sending HTTP GET ({<(met 3 req)>} bytes)"
;<  ~  bind:m
  %-  poke-our:strandio
  [%tcp %tcp-task !>(`task:tcp`[%send /get req])]
~&  ">> reading response..."
;<  body=@t  bind:m  (tcp-collect /get '' 0)
=/  len  (met 3 body)
~&  ">> total: {<len>} bytes"
%-  pure:m
!>((crip "{(scow %ud len)} bytes received"))
