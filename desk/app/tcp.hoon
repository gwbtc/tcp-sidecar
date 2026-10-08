/-  tcp
/+  default-agent, dbug
/=  t-  /ted/test-tcp-connect
/=  t-  /ted/test-tcp-get
/=  t-  /ted/test-tcp-multi
/=  t-  /ted/test-tcp-mux
/=  t-  /ted/test-tcp-get-v6
/=  t-  /ted/test-tcp-connect-v6
|%
+$  card  card:agent:gall
+$  state-0
  $:  %0
      open=(set wire)
  ==
--
%-  agent:dbug
=|  state-0
=*  state  -
^-  agent:gall
|_  =bowl:gall
+*  this  .
    def   ~(. (default-agent this %|) bowl)
::
++  on-init
  ^-  (quip card _this)
  :_  this
  [%pass /lick %arvo %l %spin /tcp]~
::
++  on-save  !>(state)
::
++  on-load
  |=  ole=vase
  ^-  (quip card _this)
  [~ this(state !<(state-0 ole))]
::
++  on-poke
  |=  [=mark =vase]
  ^-  (quip card _this)
  ?>  =(src our):bowl
  ?>  ?=(%tcp-task mark)
  =/  cmd  !<(task:tcp vase)
  :_  this
  [%pass /spit %arvo %l %spit /tcp %tcp-task cmd]~
::
++  on-arvo
  |=  [=wire sign=sign-arvo]
  ^-  (quip card _this)
  ?.  ?=([%lick %soak *] sign)  (on-arvo:def +<)
  ?:  ?=([%disconnect ~] [mark noun]:sign)
    ~&  'tcp: sidecar disconnected'
    ::  flush every open wire as %closed, not %error: a dead sidecar says
    ::  nothing about the remote peer's health, so consumers must not
    ::  attribute the fault to the peer (e.g. blacklisting it). %error is
    ::  reserved for a genuine per-connection failure from a live sidecar.
    =/  wires=(list ^wire)  ~(tap in open)
    =.  open  ~
    :-  %-  zing
        %+  turn  wires
        |=  wir=^wire
        :~  [%give %fact ~[wir] %tcp-gift !>(`gift:tcp`[%closed wir])]
            [%give %kick ~[wir] ~]
        ==
    this
  ?+    [mark noun]:sign  (on-arvo:def +<)
    [%connect ~]     ~&('tcp: sidecar connected' `this)
    [%error *]       ~&("tcp: error {(trip ;;(@t noun.sign))}" `this)
  ::
      [%tcp-gift *]
    =/  gif  (gift:tcp noun.sign)
    ?-    -.gif
        %connected
      =.  open  (~(put in open) wire.gif)
      :_  this
      [%give %fact ~[wire.gif] %tcp-gift !>(gif)]~
    ::
        %receive
      :_  this
      [%give %fact ~[wire.gif] %tcp-gift !>(gif)]~
    ::
        %closed
      =.  open  (~(del in open) wire.gif)
      :_  this
      :~  [%give %fact ~[wire.gif] %tcp-gift !>(gif)]
          [%give %kick ~[wire.gif] ~]
      ==
    ::
        %error
      =.  open  (~(del in open) wire.gif)
      :_  this
      :~  [%give %fact ~[wire.gif] %tcp-gift !>(gif)]
          [%give %kick ~[wire.gif] ~]
      ==
    ==
  ==
::
++  on-watch
  |=  =path
  ^-  (quip card _this)
  ?>  =(src our):bowl
  `this
++  on-leave  on-leave:def
++  on-peek   on-peek:def
++  on-agent  on-agent:def
++  on-fail   on-fail:def
--
