/-  tcp
/+  default-agent, dbug
/=  t-  /ted/test-tcp-connect
/=  t-  /ted/test-tcp-get
/=  t-  /ted/test-tcp-multi
/=  t-  /ted/test-tcp-mux
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
  ?>  ?=(%tcp-task mark)
  =/  cmd  !<(task:tcp vase)
  :_  this
  [%pass /spit %arvo %l %spit /tcp %tcp-task cmd]~
::
++  on-arvo
  |=  [=wire sign=sign-arvo]
  ^-  (quip card _this)
  ?.  ?=([%lick %soak *] sign)  (on-arvo:def +<)
  ?+    [mark noun]:sign  (on-arvo:def +<)
    [%connect ~]     ~&('tcp: sidecar connected' `this)
    [%disconnect ~]  ~&('tcp: sidecar disconnected' `this)
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
  `this
++  on-leave  on-leave:def
++  on-peek   on-peek:def
++  on-agent  on-agent:def
++  on-fail   on-fail:def
--
